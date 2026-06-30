/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "executor_abstract.h"

class SortExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> prev_;
    std::vector<ColMeta> sort_cols_;
    std::vector<bool> is_desc_;
    int64_t limit_;
    std::vector<std::unique_ptr<RmRecord>> tuples_;
    size_t cursor_ = 0;

   public:
    SortExecutor(std::unique_ptr<AbstractExecutor> prev,
                 const std::vector<TabCol> &sort_cols,
                 std::vector<bool> is_desc,
                 int64_t limit)
        : prev_(std::move(prev)), is_desc_(std::move(is_desc)), limit_(limit) {
        for (const auto &sort_col : sort_cols) {
            sort_cols_.push_back(prev_->get_col_offset(sort_col));
        }
    }

    void beginTuple() override {
        tuples_.clear();
        cursor_ = 0;
        for (prev_->beginTuple(); !prev_->is_end(); prev_->nextTuple()) {
            auto tuple = prev_->Next();
            if (tuple != nullptr) {
                tuples_.push_back(std::move(tuple));
            }
        }

        if (!sort_cols_.empty()) {
            std::stable_sort(
                tuples_.begin(), tuples_.end(),
                [&](const std::unique_ptr<RmRecord> &lhs,
                    const std::unique_ptr<RmRecord> &rhs) {
                    for (size_t i = 0; i < sort_cols_.size(); i++) {
                        const auto &col = sort_cols_[i];
                        int result = compare_raw(lhs->data + col.offset, rhs->data + col.offset,
                                                 col.type, col.len);
                        if (result != 0) {
                            return is_desc_[i] ? result > 0 : result < 0;
                        }
                    }
                    return false;
                });
        }
        if (limit_ >= 0 && static_cast<uint64_t>(limit_) < tuples_.size()) {
            tuples_.resize(static_cast<size_t>(limit_));
        }
    }

    void nextTuple() override {
        if (cursor_ < tuples_.size()) {
            cursor_++;
        }
    }

    std::unique_ptr<RmRecord> Next() override {
        if (is_end()) {
            return nullptr;
        }
        return std::make_unique<RmRecord>(*tuples_[cursor_]);
    }

    size_t tupleLen() const override { return prev_->tupleLen(); }
    const std::vector<ColMeta> &cols() const override { return prev_->cols(); }
    bool is_end() const override { return cursor_ >= tuples_.size(); }
    ColMeta get_col_offset(const TabCol &target) override {
        return prev_->get_col_offset(target);
    }
    Rid &rid() override { return _abstract_rid; }
};
