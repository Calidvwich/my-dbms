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
    std::unique_ptr<AbstractExecutor> left_;
    std::unique_ptr<AbstractExecutor> right_;
    size_t len_;
    std::vector<ColMeta> cols_;
    std::vector<Condition> fed_conds_;
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

    std::unique_ptr<RmRecord> make_joined_record(const RmRecord &left_record,
                                                 const RmRecord &right_record) const {
        auto joined = std::make_unique<RmRecord>(len_);
        memcpy(joined->data, left_record.data, left_->tupleLen());
        memcpy(joined->data + left_->tupleLen(), right_record.data, right_->tupleLen());
        return joined;
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
                    auto joined = make_joined_record(*left_block_[block_cursor_], *right_record_);
                    block_cursor_++;
                    if (eval_conds(*joined, cols_, fed_conds_)) {
                        current_ = std::move(joined);
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
        : left_(std::move(left)), right_(std::move(right)), fed_conds_(std::move(conds)) {
        len_ = left_->tupleLen() + right_->tupleLen();
        cols_ = left_->cols();
        auto right_cols = right_->cols();
        for (auto &col : right_cols) {
            col.offset += left_->tupleLen();
        }
        cols_.insert(cols_.end(), right_cols.begin(), right_cols.end());
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
