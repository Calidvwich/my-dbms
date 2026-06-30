/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#pragma once

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
    std::vector<std::unique_ptr<RmRecord>> results_;
    size_t cursor_ = 0;

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
        results_.clear();
        cursor_ = 0;
        for (left_->beginTuple(); !left_->is_end(); left_->nextTuple()) {
            auto left_record = left_->Next();
            for (right_->beginTuple(); !right_->is_end(); right_->nextTuple()) {
                auto right_record = right_->Next();
                auto joined = std::make_unique<RmRecord>(len_);
                memcpy(joined->data, left_record->data, left_->tupleLen());
                memcpy(joined->data + left_->tupleLen(), right_record->data, right_->tupleLen());
                if (eval_conds(*joined, cols_, fed_conds_)) {
                    results_.push_back(std::move(joined));
                }
            }
        }
    }

    void nextTuple() override {
        if (cursor_ < results_.size()) {
            cursor_++;
        }
    }

    std::unique_ptr<RmRecord> Next() override {
        if (is_end()) {
            return nullptr;
        }
        return std::make_unique<RmRecord>(*results_[cursor_]);
    }

    size_t tupleLen() const override { return len_; }
    const std::vector<ColMeta> &cols() const override { return cols_; }
    bool is_end() const override { return cursor_ >= results_.size(); }
    ColMeta get_col_offset(const TabCol &target) override { return *get_col(cols_, target); }
    Rid &rid() override { return _abstract_rid; }
};
