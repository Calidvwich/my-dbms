#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "executor_abstract.h"

class AggregateExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> prev_;
    std::vector<AggregateSpec> aggregates_;
    std::vector<ColMeta> cols_;
    std::unique_ptr<RmRecord> result_;
    size_t len_ = 0;
    bool emitted_ = false;

   public:
    AggregateExecutor(std::unique_ptr<AbstractExecutor> prev,
                      std::vector<AggregateSpec> aggregates)
        : prev_(std::move(prev)), aggregates_(std::move(aggregates)) {
        for (const auto &aggregate : aggregates_) {
            cols_.push_back(ColMeta{"", aggregate.alias, aggregate.output_type,
                                    aggregate.output_len, static_cast<int>(len_), false});
            len_ += aggregate.output_len;
        }
    }

    void beginTuple() override {
        emitted_ = false;
        result_ = std::make_unique<RmRecord>(len_);
        memset(result_->data, 0, len_);
        std::vector<bool> initialized(aggregates_.size(), false);

        for (prev_->beginTuple(); !prev_->is_end(); prev_->nextTuple()) {
            auto record = prev_->Next();
            for (size_t i = 0; i < aggregates_.size(); i++) {
                const auto &aggregate = aggregates_[i];
                char *dest = result_->data + cols_[i].offset;
                if (aggregate.type == AGGREGATE_COUNT) {
                    ++*reinterpret_cast<int64_t *>(dest);
                    continue;
                }

                auto source_col = get_col(prev_->cols(), aggregate.col);
                const char *source = record->data + source_col->offset;
                if (aggregate.type == AGGREGATE_SUM) {
                    if (aggregate.input_type == TYPE_INT) {
                        *reinterpret_cast<int64_t *>(dest) +=
                            *reinterpret_cast<const int *>(source);
                    } else {
                        *reinterpret_cast<float *>(dest) +=
                            *reinterpret_cast<const float *>(source);
                    }
                    continue;
                }

                if (!initialized[i]) {
                    memcpy(dest, source, aggregate.output_len);
                    initialized[i] = true;
                    continue;
                }
                int comparison = compare_raw(source, dest, aggregate.input_type,
                                             aggregate.input_len);
                if ((aggregate.type == AGGREGATE_MAX && comparison > 0) ||
                    (aggregate.type == AGGREGATE_MIN && comparison < 0)) {
                    memcpy(dest, source, aggregate.output_len);
                }
            }
        }
    }

    void nextTuple() override { emitted_ = true; }

    std::unique_ptr<RmRecord> Next() override {
        if (is_end()) {
            return nullptr;
        }
        return std::make_unique<RmRecord>(*result_);
    }

    size_t tupleLen() const override { return len_; }
    const std::vector<ColMeta> &cols() const override { return cols_; }
    bool is_end() const override { return emitted_; }
    ColMeta get_col_offset(const TabCol &target) override { return *get_col(cols_, target); }
    Rid &rid() override { return _abstract_rid; }
};
