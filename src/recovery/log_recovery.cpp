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

std::vector<char> make_index_key(const IndexMeta& index, const char* data) {
    std::vector<char> key(index.col_tot_len);
    int offset = 0;
    for (const auto& col : index.cols) {
        memcpy(key.data() + offset, data + col.offset, col.len);
        offset += col.len;
    }
    return key;
}

IxIndexHandle* get_index_handle(SmManager* sm_manager, const std::string& tab_name, const IndexMeta& index) {
    std::string index_name = sm_manager->get_ix_manager()->get_index_name(tab_name, index.cols);
    auto it = sm_manager->ihs_.find(index_name);
    if (it == sm_manager->ihs_.end()) {
        it = sm_manager->ihs_.emplace(index_name, sm_manager->get_ix_manager()->open_index(tab_name, index.cols)).first;
    }
    return it->second.get();
}

bool rid_matches(const std::vector<Rid>& rids, const Rid& rid) {
    return !rids.empty() && rids.front().page_no == rid.page_no && rids.front().slot_no == rid.slot_no;
}

void sync_index_insert(SmManager* sm_manager, const std::string& tab_name, const RmRecord& record, const Rid& rid) {
    auto& tab = sm_manager->db_.get_table(tab_name);
    for (const auto& index : tab.indexes) {
        auto key = make_index_key(index, record.data);
        auto* ih = get_index_handle(sm_manager, tab_name, index);
        std::vector<Rid> existing;
        if (ih->get_value(key.data(), &existing, nullptr)) {
            if (rid_matches(existing, rid)) {
                continue;
            }
            ih->delete_entry(key.data(), nullptr);
        }
        ih->insert_entry(key.data(), rid, nullptr);
    }
}

void sync_index_delete(SmManager* sm_manager, const std::string& tab_name, const RmRecord& record) {
    auto& tab = sm_manager->db_.get_table(tab_name);
    for (const auto& index : tab.indexes) {
        auto key = make_index_key(index, record.data);
        auto* ih = get_index_handle(sm_manager, tab_name, index);
        ih->delete_entry(key.data(), nullptr);
    }
}

void sync_index_update(SmManager* sm_manager, const std::string& tab_name, const RmRecord& old_record,
                       const RmRecord& new_record, const Rid& rid) {
    auto& tab = sm_manager->db_.get_table(tab_name);
    for (const auto& index : tab.indexes) {
        auto old_key = make_index_key(index, old_record.data);
        auto new_key = make_index_key(index, new_record.data);
        if (memcmp(old_key.data(), new_key.data(), index.col_tot_len) == 0) {
            continue;
        }
        auto* ih = get_index_handle(sm_manager, tab_name, index);
        ih->delete_entry(old_key.data(), nullptr);
        std::vector<Rid> existing;
        if (ih->get_value(new_key.data(), &existing, nullptr)) {
            if (rid_matches(existing, rid)) {
                continue;
            }
            ih->delete_entry(new_key.data(), nullptr);
        }
        ih->insert_entry(new_key.data(), rid, nullptr);
    }
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
        sync_index_insert(sm_manager_, tab_name, insert_log->insert_value_, insert_log->rid_);
    } else if (log->log_type_ == LogType::DELETE) {
        auto* delete_log = static_cast<const DeleteLogRecord*>(log);
        std::string tab_name = table_name_from_log(delete_log->table_name_, delete_log->table_name_size_);
        auto* fh = sm_manager_->fhs_.at(tab_name).get();
        if (safe_is_record(fh, delete_log->rid_)) {
            fh->delete_record(delete_log->rid_, nullptr);
        }
        sync_index_delete(sm_manager_, tab_name, delete_log->delete_value_);
    } else if (log->log_type_ == LogType::UPDATE) {
        auto* update_log = static_cast<const UpdateLogRecord*>(log);
        std::string tab_name = table_name_from_log(update_log->table_name_, update_log->table_name_size_);
        auto* fh = sm_manager_->fhs_.at(tab_name).get();
        if (safe_is_record(fh, update_log->rid_)) {
            fh->update_record(update_log->rid_, update_log->new_value_.data, nullptr);
        }
        sync_index_update(sm_manager_, tab_name, update_log->old_value_, update_log->new_value_, update_log->rid_);
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
    disk_manager_->reset_log();
}
