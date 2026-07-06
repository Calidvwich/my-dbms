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
#include <limits>


#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class IndexScanExecutor : public AbstractExecutor {
   private:
    std::string tab_name_;                      // 表名称
    TabMeta tab_;                               // 表的元数据
    std::vector<Condition> conds_;              // 扫描条件
    RmFileHandle *fh_;                          // 表的数据文件句柄
    std::vector<ColMeta> cols_;                 // 需要读取的字段
    size_t len_;                                // 选取出来的一条记录的长度
    std::vector<Condition> fed_conds_;          // 扫描条件，和conds_字段相同

    std::vector<std::string> index_col_names_;  // index scan涉及到的索引包含的字段
    IndexMeta index_meta_;                      // index scan涉及到的索引元数据

    Rid rid_;
    std::unique_ptr<RecScan> scan_;

    SmManager *sm_manager_;

    void fill_min(char *dest, const ColMeta &col) {
        memset(dest, 0, col.len);
        if (col.type == TYPE_INT) {
            *reinterpret_cast<int *>(dest) = std::numeric_limits<int>::lowest();
        } else if (col.type == TYPE_FLOAT) {
            *reinterpret_cast<float *>(dest) = -std::numeric_limits<float>::infinity();
        } else if (col.type == TYPE_BIGINT || col.type == TYPE_DATETIME) {
            *reinterpret_cast<int64_t *>(dest) = std::numeric_limits<int64_t>::lowest();
        }
    }

    void fill_max(char *dest, const ColMeta &col) {
        memset(dest, 0xff, col.len);
        if (col.type == TYPE_INT) {
            *reinterpret_cast<int *>(dest) = std::numeric_limits<int>::max();
        } else if (col.type == TYPE_FLOAT) {
            *reinterpret_cast<float *>(dest) = std::numeric_limits<float>::infinity();
        } else if (col.type == TYPE_BIGINT || col.type == TYPE_DATETIME) {
            *reinterpret_cast<int64_t *>(dest) = std::numeric_limits<int64_t>::max();
        }
    }

   public:
    IndexScanExecutor(SmManager *sm_manager, std::string tab_name, std::vector<Condition> conds, std::vector<std::string> index_col_names,
                    Context *context) {
        sm_manager_ = sm_manager;
        context_ = context;
        tab_name_ = std::move(tab_name);
        tab_ = sm_manager_->db_.get_table(tab_name_);
        conds_ = std::move(conds);
        // index_no_ = index_no;
        index_col_names_ = index_col_names; 
        index_meta_ = *(tab_.get_index_meta(index_col_names_));
        fh_ = sm_manager_->fhs_.at(tab_name_).get();
        cols_ = tab_.cols;
        len_ = cols_.back().offset + cols_.back().len;
        if (context_ != nullptr && context_->lock_mgr_ != nullptr && context_->txn_ != nullptr) {
            context_->lock_mgr_->lock_shared_on_table(context_->txn_, fh_->GetFd());
        }
        std::map<CompOp, CompOp> swap_op = {
            {OP_EQ, OP_EQ}, {OP_NE, OP_NE}, {OP_LT, OP_GT}, {OP_GT, OP_LT}, {OP_LE, OP_GE}, {OP_GE, OP_LE},
        };

        for (auto &cond : conds_) {
            if (cond.lhs_col.tab_name != tab_name_) {
                // lhs is on other table, now rhs must be on this table
                if (cond.is_rhs_val || cond.rhs_col.tab_name != tab_name_) {
                    throw InternalError("Invalid index scan condition");
                }
                // swap lhs and rhs
                std::swap(cond.lhs_col, cond.rhs_col);
                cond.op = swap_op.at(cond.op);
            }
        }
        fed_conds_ = conds_;
    }

    void beginTuple() override {
        auto index_name = sm_manager_->get_ix_manager()->get_index_name(tab_name_, index_meta_.cols);
        auto *ih = sm_manager_->ihs_.at(index_name).get();
        std::vector<char> lower(index_meta_.col_tot_len);
        std::vector<char> upper(index_meta_.col_tot_len);
        int offset = 0;
        bool lower_strict = false;
        bool upper_inclusive = true;
        bool prefix_open = true;

        for (const auto &col : index_meta_.cols) {
            fill_min(lower.data() + offset, col);
            fill_max(upper.data() + offset, col);
            if (prefix_open) {
                std::vector<const Condition *> col_conds;
                for (const auto &cond : fed_conds_) {
                    if (cond.is_rhs_val && cond.lhs_col.tab_name == tab_name_ &&
                        cond.lhs_col.col_name == col.name && cond.op != OP_NE) {
                        col_conds.push_back(&cond);
                    }
                }
                auto equality = std::find_if(col_conds.begin(), col_conds.end(),
                                             [](const Condition *cond) { return cond->op == OP_EQ; });
                if (equality != col_conds.end()) {
                    memcpy(lower.data() + offset, (*equality)->rhs_val.raw->data, col.len);
                    memcpy(upper.data() + offset, (*equality)->rhs_val.raw->data, col.len);
                } else if (!col_conds.empty()) {
                    for (const Condition *cond : col_conds) {
                        if (cond->op == OP_GT || cond->op == OP_GE) {
                            memcpy(lower.data() + offset, cond->rhs_val.raw->data, col.len);
                            lower_strict = cond->op == OP_GT;
                        } else if (cond->op == OP_LT || cond->op == OP_LE) {
                            memcpy(upper.data() + offset, cond->rhs_val.raw->data, col.len);
                            upper_inclusive = cond->op == OP_LE;
                        }
                    }
                    prefix_open = false;
                } else {
                    prefix_open = false;
                }
            }
            offset += col.len;
        }

        Iid begin = lower_strict ? ih->upper_bound(lower.data()) : ih->lower_bound(lower.data());
        Iid end = upper_inclusive ? ih->upper_bound(upper.data()) : ih->lower_bound(upper.data());
        scan_ = std::make_unique<IxScan>(ih, begin, end, sm_manager_->get_bpm());
        while (!scan_->is_end()) {
            auto record = fh_->get_record(scan_->rid(), context_);
            if (eval_conds(*record, cols_, fed_conds_)) {
                rid_ = scan_->rid();
                return;
            }
            scan_->next();
        }
    }

    void nextTuple() override {
        if (scan_ == nullptr || scan_->is_end()) {
            return;
        }
        scan_->next();
        while (!scan_->is_end()) {
            auto record = fh_->get_record(scan_->rid(), context_);
            if (eval_conds(*record, cols_, fed_conds_)) {
                rid_ = scan_->rid();
                return;
            }
            scan_->next();
        }
    }

    std::unique_ptr<RmRecord> Next() override {
        if (is_end()) {
            return nullptr;
        }
        return fh_->get_record(rid_, context_);
    }

    size_t tupleLen() const override { return len_; }
    const std::vector<ColMeta> &cols() const override { return cols_; }
    bool is_end() const override { return scan_ == nullptr || scan_->is_end(); }
    ColMeta get_col_offset(const TabCol &target) override { return *get_col(cols_, target); }
    Rid &rid() override { return rid_; }
};
