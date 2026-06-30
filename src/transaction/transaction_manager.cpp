/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#include "transaction_manager.h"

#include <cstring>
#include <vector>

std::unordered_map<txn_id_t, Transaction *> TransactionManager::txn_map = {};

namespace {

std::vector<char> make_index_key(const IndexMeta &index, const RmRecord &record) {
    std::vector<char> key(index.col_tot_len);
    int offset = 0;
    for (const auto &col : index.cols) {
        memcpy(key.data() + offset, record.data + col.offset, col.len);
        offset += col.len;
    }
    return key;
}

void delete_index_entries(SmManager *sm_manager, const std::string &tab_name, const TabMeta &tab,
                          const RmRecord &record) {
    for (const auto &index : tab.indexes) {
        auto key = make_index_key(index, record);
        auto *ih = sm_manager->ihs_.at(sm_manager->get_ix_manager()->get_index_name(tab_name, index.cols)).get();
        ih->delete_entry(key.data(), nullptr);
    }
}

void insert_index_entries(SmManager *sm_manager, const std::string &tab_name, const TabMeta &tab,
                          const RmRecord &record, const Rid &rid) {
    for (const auto &index : tab.indexes) {
        auto key = make_index_key(index, record);
        auto *ih = sm_manager->ihs_.at(sm_manager->get_ix_manager()->get_index_name(tab_name, index.cols)).get();
        if (ih->insert_entry(key.data(), rid, nullptr) == IX_NO_PAGE) {
            throw UniqueConstraintError();
        }
    }
}

}  // namespace

Transaction *TransactionManager::begin(Transaction *txn, LogManager *log_manager) {
    if (txn == nullptr) {
        txn = new Transaction(next_txn_id_++);
    }
    txn->set_start_ts(next_timestamp_++);
    txn->set_state(TransactionState::GROWING);
    if (log_manager != nullptr) {
        BeginLogRecord log(txn->get_transaction_id());
        log.prev_lsn_ = txn->get_prev_lsn();
        lsn_t lsn = log_manager->add_log_to_buffer(&log);
        txn->set_prev_lsn(lsn);
    }
    std::scoped_lock lock{latch_};
    txn_map[txn->get_transaction_id()] = txn;
    return txn;
}

void TransactionManager::commit(Transaction *txn, LogManager *log_manager) {
    if (txn == nullptr) {
        return;
    }
    if (log_manager != nullptr) {
        CommitLogRecord log(txn->get_transaction_id());
        log.prev_lsn_ = txn->get_prev_lsn();
        lsn_t lsn = log_manager->add_log_to_buffer(&log);
        txn->set_prev_lsn(lsn);
        log_manager->flush_log_to_disk();
    }
    std::vector<LockDataId> locks(txn->get_lock_set()->begin(), txn->get_lock_set()->end());
    for (const auto &lock : locks) {
        lock_manager_->unlock(txn, lock);
    }
    txn->get_lock_set()->clear();
    for (auto *record : *txn->get_write_set()) {
        delete record;
    }
    txn->get_write_set()->clear();
    txn->set_state(TransactionState::COMMITTED);
}

void TransactionManager::abort(Transaction *txn, LogManager *log_manager) {
    if (txn == nullptr) {
        return;
    }
    auto write_set = txn->get_write_set();
    for (auto it = write_set->rbegin(); it != write_set->rend(); ++it) {
        WriteRecord *write_record = *it;
        const std::string &tab_name = write_record->GetTableName();
        TabMeta tab = sm_manager_->db_.get_table(tab_name);
        auto *fh = sm_manager_->fhs_.at(tab_name).get();
        Rid rid = write_record->GetRid();

        switch (write_record->GetWriteType()) {
            case WType::INSERT_TUPLE: {
                auto record = fh->get_record(rid, nullptr);
                delete_index_entries(sm_manager_, tab_name, tab, *record);
                fh->delete_record(rid, nullptr);
                break;
            }
            case WType::DELETE_TUPLE: {
                RmRecord &old_record = write_record->GetRecord();
                fh->insert_record(rid, old_record.data);
                insert_index_entries(sm_manager_, tab_name, tab, old_record, rid);
                break;
            }
            case WType::UPDATE_TUPLE: {
                auto current_record = fh->get_record(rid, nullptr);
                RmRecord &old_record = write_record->GetRecord();
                delete_index_entries(sm_manager_, tab_name, tab, *current_record);
                fh->update_record(rid, old_record.data, nullptr);
                insert_index_entries(sm_manager_, tab_name, tab, old_record, rid);
                break;
            }
        }
    }
    for (auto *record : *write_set) {
        delete record;
    }
    write_set->clear();

    if (log_manager != nullptr) {
        AbortLogRecord log(txn->get_transaction_id());
        log.prev_lsn_ = txn->get_prev_lsn();
        lsn_t lsn = log_manager->add_log_to_buffer(&log);
        txn->set_prev_lsn(lsn);
        log_manager->flush_log_to_disk();
    }

    std::vector<LockDataId> locks(txn->get_lock_set()->begin(), txn->get_lock_set()->end());
    for (const auto &lock : locks) {
        lock_manager_->unlock(txn, lock);
    }
    txn->get_lock_set()->clear();
    txn->set_state(TransactionState::ABORTED);
}
