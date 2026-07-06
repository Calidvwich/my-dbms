/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#include "log_recovery.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "record/rm.h"
#include "record/rm_scan.h"

namespace {

std::string table_name_from_log(const char* table_name, size_t size) {
    return std::string(table_name, table_name + size);
}

std::vector<char> make_index_key(const IndexMeta& index, const RmRecord& record) {
    std::vector<char> key(index.col_tot_len);
    int offset = 0;
    for (const auto& col : index.cols) {
        memcpy(key.data() + offset, record.data + col.offset, col.len);
        offset += col.len;
    }
    return key;
}

bool safe_is_record(RmFileHandle* fh, const Rid& rid) {
    try {
        return fh->is_record(rid);
    } catch (RMDBError&) {
        return false;
    }
}

}  // namespace

void RecoveryManager::load_logs() {
    logs_.clear();
    committed_txns_.clear();
    aborted_txns_.clear();
    active_txns_.clear();

    int file_size = disk_manager_->get_file_size(LOG_FILE_NAME);
    if (file_size <= 0) {
        return;
    }
    std::vector<char> data(file_size);
    int read_size = disk_manager_->read_log(data.data(), file_size, 0);
    if (read_size <= 0) {
        return;
    }

    int offset = 0;
    while (offset + LOG_HEADER_SIZE <= read_size) {
        LogType type = *reinterpret_cast<LogType*>(data.data() + offset + OFFSET_LOG_TYPE);
        uint32_t len = *reinterpret_cast<uint32_t*>(data.data() + offset + OFFSET_LOG_TOT_LEN);
        if (len < LOG_HEADER_SIZE || offset + static_cast<int>(len) > read_size) {
            break;
        }

        std::shared_ptr<LogRecord> record;
        switch (type) {
            case LogType::begin:
                record = std::make_shared<BeginLogRecord>();
                break;
            case LogType::commit:
                record = std::make_shared<CommitLogRecord>();
                break;
            case LogType::ABORT:
                record = std::make_shared<AbortLogRecord>();
                break;
            case LogType::INSERT:
                record = std::make_shared<InsertLogRecord>();
                break;
            case LogType::DELETE:
                record = std::make_shared<DeleteLogRecord>();
                break;
            case LogType::UPDATE:
                record = std::make_shared<UpdateLogRecord>();
                break;
        }
        record->deserialize(data.data() + offset);
        logs_.push_back(record);

        if (record->log_type_ == LogType::begin) {
            active_txns_.insert(record->log_tid_);
        } else if (record->log_type_ == LogType::commit) {
            committed_txns_.insert(record->log_tid_);
            active_txns_.erase(record->log_tid_);
        } else if (record->log_type_ == LogType::ABORT) {
            aborted_txns_.insert(record->log_tid_);
            active_txns_.erase(record->log_tid_);
        }
        offset += static_cast<int>(len);
    }
}

void RecoveryManager::analyze() {
    load_logs();
}

void RecoveryManager::redo_log(const LogRecord* log) {
    if (log->log_type_ == LogType::INSERT) {
        auto* insert_log = static_cast<const InsertLogRecord*>(log);
        std::string tab_name = table_name_from_log(insert_log->table_name_, insert_log->table_name_size_);
        auto* fh = sm_manager_->fhs_.at(tab_name).get();
        if (safe_is_record(fh, insert_log->rid_)) {
            fh->update_record(insert_log->rid_, insert_log->insert_value_.data, nullptr);
        } else {
            try {
                fh->insert_record(insert_log->rid_, insert_log->insert_value_.data);
            } catch (RMDBError&) {
                fh->insert_record(insert_log->insert_value_.data, nullptr);
            }
        }
    } else if (log->log_type_ == LogType::DELETE) {
        auto* delete_log = static_cast<const DeleteLogRecord*>(log);
        std::string tab_name = table_name_from_log(delete_log->table_name_, delete_log->table_name_size_);
        auto* fh = sm_manager_->fhs_.at(tab_name).get();
        if (safe_is_record(fh, delete_log->rid_)) {
            fh->delete_record(delete_log->rid_, nullptr);
        }
    } else if (log->log_type_ == LogType::UPDATE) {
        auto* update_log = static_cast<const UpdateLogRecord*>(log);
        std::string tab_name = table_name_from_log(update_log->table_name_, update_log->table_name_size_);
        auto* fh = sm_manager_->fhs_.at(tab_name).get();
        if (safe_is_record(fh, update_log->rid_)) {
            fh->update_record(update_log->rid_, update_log->new_value_.data, nullptr);
        }
    }
}

void RecoveryManager::redo() {
    for (const auto& log : logs_) {
        if (committed_txns_.count(log->log_tid_) == 0) {
            continue;
        }
        redo_log(log.get());
    }
}

void RecoveryManager::undo_log(const LogRecord* log) {
    if (log->log_type_ == LogType::INSERT) {
        auto* insert_log = static_cast<const InsertLogRecord*>(log);
        std::string tab_name = table_name_from_log(insert_log->table_name_, insert_log->table_name_size_);
        auto* fh = sm_manager_->fhs_.at(tab_name).get();
        if (safe_is_record(fh, insert_log->rid_)) {
            fh->delete_record(insert_log->rid_, nullptr);
        }
    } else if (log->log_type_ == LogType::DELETE) {
        auto* delete_log = static_cast<const DeleteLogRecord*>(log);
        std::string tab_name = table_name_from_log(delete_log->table_name_, delete_log->table_name_size_);
        auto* fh = sm_manager_->fhs_.at(tab_name).get();
        if (safe_is_record(fh, delete_log->rid_)) {
            fh->update_record(delete_log->rid_, delete_log->delete_value_.data, nullptr);
        } else {
            try {
                fh->insert_record(delete_log->rid_, delete_log->delete_value_.data);
            } catch (RMDBError&) {
                fh->insert_record(delete_log->delete_value_.data, nullptr);
            }
        }
    } else if (log->log_type_ == LogType::UPDATE) {
        auto* update_log = static_cast<const UpdateLogRecord*>(log);
        std::string tab_name = table_name_from_log(update_log->table_name_, update_log->table_name_size_);
        auto* fh = sm_manager_->fhs_.at(tab_name).get();
        if (safe_is_record(fh, update_log->rid_)) {
            fh->update_record(update_log->rid_, update_log->old_value_.data, nullptr);
        }
    }
}

void RecoveryManager::undo() {
    for (auto it = logs_.rbegin(); it != logs_.rend(); ++it) {
        const auto& log = *it;
        if (active_txns_.count(log->log_tid_) == 0) {
            continue;
        }
        undo_log(log.get());
    }
    rebuild_indexes();
    flush_all();
    disk_manager_->reset_log();
}

void RecoveryManager::rebuild_indexes() {
    for (auto& table_entry : sm_manager_->db_.tabs()) {
        const std::string& tab_name = table_entry.first;
        TabMeta& tab = table_entry.second;
        for (const auto& index : tab.indexes) {
            std::string index_name = sm_manager_->get_ix_manager()->get_index_name(tab_name, index.cols);
            if (sm_manager_->ihs_.find(index_name) == sm_manager_->ihs_.end()) {
                sm_manager_->ihs_[index_name] = sm_manager_->get_ix_manager()->open_index(tab_name, index.cols);
            }
            auto* index_handle = sm_manager_->ihs_.at(index_name).get();
            index_handle->clear_entries();
            RmScan scan(sm_manager_->fhs_.at(tab_name).get());
            while (!scan.is_end()) {
                auto record = sm_manager_->fhs_.at(tab_name)->get_record(scan.rid(), nullptr);
                auto key = make_index_key(index, *record);
                try {
                    index_handle->insert_entry(key.data(), scan.rid(), nullptr);
                } catch (RMDBError&) {
                    // Recovery should not abort on a stale duplicate index entry; the table
                    // contents are authoritative and the in-memory index is only a cache.
                }
                scan.next();
            }
        }
    }
}

void RecoveryManager::flush_all() {
    sm_manager_->flush_meta();
    for (auto& entry : sm_manager_->fhs_) {
        sm_manager_->get_bpm()->flush_all_pages(entry.second->GetFd());
    }
}
