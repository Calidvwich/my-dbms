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
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class UpdateExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;
    std::vector<Condition> conds_;
    RmFileHandle *fh_;
    std::vector<Rid> rids_;
    std::string tab_name_;
    std::vector<SetClause> set_clauses_;
    SmManager *sm_manager_;

   public:
    UpdateExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<SetClause> set_clauses,
                   std::vector<Condition> conds, std::vector<Rid> rids, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = tab_name;
        set_clauses_ = set_clauses;
        tab_ = sm_manager_->db_.get_table(tab_name);
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        conds_ = conds;
        rids_ = rids;
        context_ = context;
    }
    std::unique_ptr<RmRecord> Next() override {
        if (context_ != nullptr && context_->lock_mgr_ != nullptr && context_->txn_ != nullptr) {
            context_->lock_mgr_->lock_exclusive_on_table(context_->txn_, fh_->GetFd());
        }
        std::vector<std::unique_ptr<RmRecord>> old_records;
        std::vector<std::unique_ptr<RmRecord>> new_records;
        for (const auto &rid : rids_) {
            auto old_record = fh_->get_record(rid, context_);
            auto record = std::make_unique<RmRecord>(*old_record);
            for (const auto &set_clause : set_clauses_) {
                auto col = tab_.get_col(set_clause.lhs.col_name);
                memcpy(record->data + col->offset, set_clause.rhs.raw->data, col->len);
            }
            old_records.push_back(std::move(old_record));
            new_records.push_back(std::move(record));
        }

        for (const auto &index : tab_.indexes) {
            auto *ih = sm_manager_->ihs_.at(
                sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
            std::vector<ColType> types;
            std::vector<int> lengths;
            for (const auto &col : index.cols) {
                types.push_back(col.type);
                lengths.push_back(col.len);
            }
            std::vector<std::vector<char>> new_keys;
            for (const auto &record : new_records) {
                std::vector<char> key(index.col_tot_len);
                int offset = 0;
                for (const auto &col : index.cols) {
                    memcpy(key.data() + offset, record->data + col.offset, col.len);
                    offset += col.len;
                }
                for (const auto &other : new_keys) {
                    if (ix_compare(key.data(), other.data(), types, lengths) == 0) {
                        throw UniqueConstraintError();
                    }
                }
                std::vector<Rid> existing;
                if (ih->get_value(key.data(), &existing, context_ == nullptr ? nullptr : context_->txn_)) {
                    bool belongs_to_batch =
                        std::find(rids_.begin(), rids_.end(), existing.front()) != rids_.end();
                    if (!belongs_to_batch) {
                        throw UniqueConstraintError();
                    }
                }
                new_keys.push_back(std::move(key));
            }
        }

        if (context_ != nullptr && context_->txn_ != nullptr) {
            for (size_t row = 0; row < rids_.size(); row++) {
                context_->txn_->append_write_record(
                    new WriteRecord(WType::UPDATE_TUPLE, tab_name_, rids_[row], *old_records[row]));
                if (context_->log_mgr_ != nullptr) {
                    Rid logged_rid = rids_[row];
                    UpdateLogRecord log(context_->txn_->get_transaction_id(), *old_records[row],
                                        *new_records[row], logged_rid, tab_name_);
                    log.prev_lsn_ = context_->txn_->get_prev_lsn();
                    lsn_t lsn = context_->log_mgr_->add_log_to_buffer(&log);
                    context_->txn_->set_prev_lsn(lsn);
                    context_->log_mgr_->flush_log_to_disk();
                }
            }
        }

        for (const auto &index : tab_.indexes) {
            auto *ih = sm_manager_->ihs_.at(
                sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
            std::vector<std::vector<char>> old_keys;
            std::vector<std::vector<char>> new_keys;
            std::vector<bool> changed;
            for (size_t row = 0; row < rids_.size(); row++) {
                std::vector<char> old_key(index.col_tot_len);
                std::vector<char> new_key(index.col_tot_len);
                int offset = 0;
                for (const auto &col : index.cols) {
                    memcpy(old_key.data() + offset, old_records[row]->data + col.offset, col.len);
                    memcpy(new_key.data() + offset, new_records[row]->data + col.offset, col.len);
                    offset += col.len;
                }
                changed.push_back(memcmp(old_key.data(), new_key.data(), index.col_tot_len) != 0);
                old_keys.push_back(std::move(old_key));
                new_keys.push_back(std::move(new_key));
            }
            for (size_t row = 0; row < rids_.size(); row++) {
                if (changed[row]) {
                    ih->delete_entry(old_keys[row].data(), context_ == nullptr ? nullptr : context_->txn_);
                }
            }
            for (size_t row = 0; row < rids_.size(); row++) {
                if (changed[row]) {
                    if (ih->insert_entry(new_keys[row].data(), rids_[row],
                                         context_ == nullptr ? nullptr : context_->txn_) == IX_NO_PAGE) {
                        throw UniqueConstraintError();
                    }
                }
            }
        }
        for (size_t row = 0; row < rids_.size(); row++) {
            fh_->update_record(rids_[row], new_records[row]->data, context_);
        }
        return nullptr;
    }

    Rid &rid() override { return _abstract_rid; }
};
