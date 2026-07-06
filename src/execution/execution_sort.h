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
    bool input_sorted_;
    bool stream_mode_ = false;
    size_t stream_count_ = 0;
    std::unique_ptr<RmRecord> stream_tuple_;
    std::vector<std::unique_ptr<RmRecord>> tuples_;
    size_t cursor_ = 0;

   public:
    SortExecutor(std::unique_ptr<AbstractExecutor> prev,
                 const std::vector<TabCol> &sort_cols,
                 std::vector<bool> is_desc,
                 int64_t limit,
                 bool input_sorted)
        : prev_(std::move(prev)), is_desc_(std::move(is_desc)), limit_(limit), input_sorted_(input_sorted) {
        for (const auto &sort_col : sort_cols) {
            sort_cols_.push_back(prev_->get_col_offset(sort_col));
        }
    }

    void beginTuple() override {
        tuples_.clear();
        cursor_ = 0;
        if (input_sorted_) {
            stream_mode_ = true;
            stream_count_ = 0;
            prev_->beginTuple();
            stream_tuple_.reset();
            if (!prev_->is_end() && (limit_ < 0 || stream_count_ < static_cast<size_t>(limit_))) {
                stream_tuple_ = prev_->Next();
            }
            return;
        }
        if (limit_ >= 0 && sort_cols_.empty()) {
            for (prev_->beginTuple(); !prev_->is_end() && static_cast<int64_t>(tuples_.size()) < limit_; prev_->nextTuple()) {
                auto tuple = prev_->Next();
                if (tuple != nullptr) {
                    tuples_.push_back(std::move(tuple));
                }
            }
            return;
        }

        for (prev_->beginTuple(); !prev_->is_end(); prev_->nextTuple()) {
            auto tuple = prev_->Next();
            if (tuple != nullptr) {
                tuples_.push_back(std::move(tuple));
            }
        }

        if (!sort_cols_.empty()) {
            auto less = [&](const std::unique_ptr<RmRecord> &lhs,
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
            };

            if (limit_ >= 0 && static_cast<uint64_t>(limit_) < tuples_.size()) {
                std::partial_sort(tuples_.begin(), tuples_.begin() + limit_, tuples_.end(), less);
                tuples_.resize(static_cast<size_t>(limit_));
            } else {
                std::sort(tuples_.begin(), tuples_.end(), less);
            }
        }
    }

    void nextTuple() override {
        if (stream_mode_) {
            if (stream_tuple_ == nullptr) {
                return;
            }
            stream_count_++;
            if (limit_ >= 0 && stream_count_ >= static_cast<size_t>(limit_)) {
                stream_tuple_.reset();
                return;
            }
            prev_->nextTuple();
            if (prev_->is_end()) {
                stream_tuple_.reset();
            } else {
                stream_tuple_ = prev_->Next();
                if (stream_tuple_ == nullptr) {
                    stream_tuple_.reset();
                }
            }
            return;
        }
        if (cursor_ < tuples_.size()) {
            cursor_++;
        }
    }

    std::unique_ptr<RmRecord> Next() override {
        if (is_end()) {
            return nullptr;
        }
        if (stream_mode_) {
            return std::make_unique<RmRecord>(*stream_tuple_);
        }
        return std::make_unique<RmRecord>(*tuples_[cursor_]);
    }

    size_t tupleLen() const override { return prev_->tupleLen(); }
    const std::vector<ColMeta> &cols() const override { return prev_->cols(); }
    bool is_end() const override { return stream_mode_ ? stream_tuple_ == nullptr : cursor_ >= tuples_.size(); }
    ColMeta get_col_offset(const TabCol &target) override {
        return prev_->get_col_offset(target);
    }
    Rid &rid() override { return _abstract_rid; }
};
