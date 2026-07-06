/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#pragma once

#include <algorithm>

#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class NestedLoopJoinExecutor : public AbstractExecutor {
   private:
    struct JoinCondItem {
        ColMeta lhs_col;
        ColMeta rhs_col;
        bool lhs_from_left;
        bool rhs_from_left;
        CompOp op;
    };

    std::unique_ptr<AbstractExecutor> left_;
    std::unique_ptr<AbstractExecutor> right_;
    size_t len_;
    std::vector<ColMeta> cols_;
    std::vector<Condition> raw_conds_;
    std::vector<JoinCondItem> fed_conds_;
    std::vector<std::unique_ptr<RmRecord>> left_block_;
    size_t block_cursor_ = 0;
    std::unique_ptr<RmRecord> right_record_;
    std::unique_ptr<RmRecord> current_;
    bool end_ = true;

    static constexpr size_t JOIN_BUFFER_SIZE = 256 * 1024 * 1024;

    bool load_left_block() {
        left_block_.clear();
        block_cursor_ = 0;
        size_t used = 0;
        size_t left_len = std::max<size_t>(1, left_->tupleLen());
        size_t max_records = std::max<size_t>(1, JOIN_BUFFER_SIZE / left_len);

        while (!left_->is_end() && left_block_.size() < max_records && used + left_len <= JOIN_BUFFER_SIZE) {
            auto record = left_->Next();
            if (record != nullptr) {
                used += left_len;
                left_block_.push_back(std::move(record));
            }
            left_->nextTuple();
        }
        return !left_block_.empty();
    }

    bool eval_join_conds(const RmRecord &left_record, const RmRecord &right_record) const {
        for (const auto &cond : fed_conds_) {
            const char *lhs_data = cond.lhs_from_left ? left_record.data + cond.lhs_col.offset
                                                      : right_record.data + cond.lhs_col.offset;
            const char *rhs_data = cond.rhs_from_left ? left_record.data + cond.rhs_col.offset
                                                      : right_record.data + cond.rhs_col.offset;
            int result = compare_raw(lhs_data, rhs_data, cond.lhs_col.type, cond.lhs_col.len);
            if (!compare_result(result, cond.op)) {
                return false;
            }
        }
        return true;
    }

    void find_next_match() {
        current_.reset();
        while (true) {
            if (left_block_.empty()) {
                end_ = true;
                return;
            }

            while (!right_->is_end()) {
                if (right_record_ == nullptr) {
                    right_record_ = right_->Next();
                    block_cursor_ = 0;
                }
                if (right_record_ == nullptr) {
                    right_->nextTuple();
                    continue;
                }

                while (block_cursor_ < left_block_.size()) {
                    auto &left_record = *left_block_[block_cursor_++];
                    if (eval_join_conds(left_record, *right_record_)) {
                        current_ = std::make_unique<RmRecord>(len_);
                        memcpy(current_->data, left_record.data, left_->tupleLen());
                        memcpy(current_->data + left_->tupleLen(), right_record_->data, right_->tupleLen());
                        end_ = false;
                        return;
                    }
                }

                right_record_.reset();
                block_cursor_ = 0;
                right_->nextTuple();
            }

            if (!load_left_block()) {
                end_ = true;
                return;
            }
            right_->beginTuple();
            right_record_.reset();
        }
    }

   public:
    NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left, std::unique_ptr<AbstractExecutor> right,
                           std::vector<Condition> conds)
        : left_(std::move(left)), right_(std::move(right)), raw_conds_(std::move(conds)) {
        len_ = left_->tupleLen() + right_->tupleLen();
        cols_ = left_->cols();
        auto right_cols = right_->cols();
        for (auto &col : right_cols) {
            col.offset += left_->tupleLen();
        }
        cols_.insert(cols_.end(), right_cols.begin(), right_cols.end());

        std::vector<JoinCondItem> resolved_conds;
        resolved_conds.reserve(raw_conds_.size());
        for (const auto &cond : raw_conds_) {
            JoinCondItem item{};
            auto lhs_left = std::find_if(left_->cols().begin(), left_->cols().end(), [&](const ColMeta &col) {
                return col.tab_name == cond.lhs_col.tab_name && col.name == cond.lhs_col.col_name;
            });
            auto lhs_right = std::find_if(right_->cols().begin(), right_->cols().end(), [&](const ColMeta &col) {
                return col.tab_name == cond.lhs_col.tab_name && col.name == cond.lhs_col.col_name;
            });
            auto rhs_left = std::find_if(left_->cols().begin(), left_->cols().end(), [&](const ColMeta &col) {
                return col.tab_name == cond.rhs_col.tab_name && col.name == cond.rhs_col.col_name;
            });
            auto rhs_right = std::find_if(right_->cols().begin(), right_->cols().end(), [&](const ColMeta &col) {
                return col.tab_name == cond.rhs_col.tab_name && col.name == cond.rhs_col.col_name;
            });

            bool lhs_found = lhs_left != left_->cols().end() || lhs_right != right_->cols().end();
            bool rhs_found = rhs_left != left_->cols().end() || rhs_right != right_->cols().end();
            if (!lhs_found || !rhs_found) {
                throw InternalError("Invalid join condition");
            }

            if (lhs_left != left_->cols().end()) {
                item.lhs_col = *lhs_left;
                item.lhs_from_left = true;
            } else {
                item.lhs_col = *lhs_right;
                item.lhs_from_left = false;
            }

            if (rhs_left != left_->cols().end()) {
                item.rhs_col = *rhs_left;
                item.rhs_from_left = true;
            } else {
                item.rhs_col = *rhs_right;
                item.rhs_from_left = false;
            }

            if (item.lhs_from_left == item.rhs_from_left) {
                throw InternalError("Join condition must compare columns from different inputs");
            }

            item.op = cond.op;
            resolved_conds.push_back(item);
        }
        fed_conds_ = std::move(resolved_conds);
    }

    void beginTuple() override {
        left_->beginTuple();
        end_ = !load_left_block();
        right_->beginTuple();
        right_record_.reset();
        if (!end_) {
            find_next_match();
        }
    }

    void nextTuple() override {
        if (!end_) {
            find_next_match();
        }
    }

    std::unique_ptr<RmRecord> Next() override {
        if (is_end()) {
            return nullptr;
        }
        return std::make_unique<RmRecord>(*current_);
    }

    size_t tupleLen() const override { return len_; }
    const std::vector<ColMeta> &cols() const override { return cols_; }
    bool is_end() const override { return end_; }
    ColMeta get_col_offset(const TabCol &target) override { return *get_col(cols_, target); }
    Rid &rid() override { return _abstract_rid; }
};
