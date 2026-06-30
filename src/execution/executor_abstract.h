/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include "execution_defs.h"
#include "common/common.h"
#include "index/ix.h"
#include "system/sm.h"

class AbstractExecutor {
   public:
    Rid _abstract_rid;

    Context *context_;

    virtual ~AbstractExecutor() = default;

    virtual size_t tupleLen() const { return 0; };

    virtual const std::vector<ColMeta> &cols() const {
        std::vector<ColMeta> *_cols = nullptr;
        return *_cols;
    };

    virtual std::string getType() { return "AbstractExecutor"; };

    virtual void beginTuple(){};

    virtual void nextTuple(){};

    virtual bool is_end() const { return true; };

    virtual Rid &rid() = 0;

    virtual std::unique_ptr<RmRecord> Next() = 0;

    virtual ColMeta get_col_offset(const TabCol &target) { return ColMeta();};

    static int compare_raw(const char *lhs, const char *rhs, ColType type, int len) {
        if (type == TYPE_INT) {
            int lhs_val = *reinterpret_cast<const int *>(lhs);
            int rhs_val = *reinterpret_cast<const int *>(rhs);
            return (lhs_val > rhs_val) - (lhs_val < rhs_val);
        }
        if (type == TYPE_FLOAT) {
            float lhs_val = *reinterpret_cast<const float *>(lhs);
            float rhs_val = *reinterpret_cast<const float *>(rhs);
            return (lhs_val > rhs_val) - (lhs_val < rhs_val);
        }
        if (type == TYPE_BIGINT || type == TYPE_DATETIME) {
            int64_t lhs_val = *reinterpret_cast<const int64_t *>(lhs);
            int64_t rhs_val = *reinterpret_cast<const int64_t *>(rhs);
            return (lhs_val > rhs_val) - (lhs_val < rhs_val);
        }
        int result = memcmp(lhs, rhs, len);
        return (result > 0) - (result < 0);
    }

    static bool compare_result(int result, CompOp op) {
        switch (op) {
            case OP_EQ: return result == 0;
            case OP_NE: return result != 0;
            case OP_LT: return result < 0;
            case OP_GT: return result > 0;
            case OP_LE: return result <= 0;
            case OP_GE: return result >= 0;
        }
        return false;
    }

    bool eval_conds(const RmRecord &record, const std::vector<ColMeta> &record_cols,
                    const std::vector<Condition> &conds) const {
        for (const auto &cond : conds) {
            auto lhs = get_col(record_cols, cond.lhs_col);
            const char *lhs_data = record.data + lhs->offset;
            const char *rhs_data;
            if (cond.is_rhs_val) {
                rhs_data = cond.rhs_val.raw->data;
            } else {
                auto rhs = get_col(record_cols, cond.rhs_col);
                if (lhs->type != rhs->type || lhs->len != rhs->len) {
                    return false;
                }
                rhs_data = record.data + rhs->offset;
            }
            if (!compare_result(compare_raw(lhs_data, rhs_data, lhs->type, lhs->len), cond.op)) {
                return false;
            }
        }
        return true;
    }

    std::vector<ColMeta>::const_iterator get_col(const std::vector<ColMeta> &rec_cols,
                                                 const TabCol &target) const {
        auto pos = std::find_if(rec_cols.begin(), rec_cols.end(), [&](const ColMeta &col) {
            return col.tab_name == target.tab_name && col.name == target.col_name;
        });
        if (pos == rec_cols.end()) {
            throw ColumnNotFoundError(target.tab_name + '.' + target.col_name);
        }
        return pos;
    }
};
