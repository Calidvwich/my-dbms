/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#include "lock_manager.h"

#include <algorithm>
#include "transaction/transaction_manager.h"

bool LockManager::compatible(const LockRequestQueue& queue, txn_id_t txn_id, LockMode lock_mode) const {
    for (const auto& request : queue.request_queue_) {
        if (!request.granted_ || request.txn_id_ == txn_id) {
            continue;
        }
        bool both_shared = request.lock_mode_ == LockMode::SHARED && lock_mode == LockMode::SHARED;
        bool both_intention_shared =
            request.lock_mode_ == LockMode::INTENTION_SHARED && lock_mode == LockMode::INTENTION_SHARED;
        if (!both_shared && !both_intention_shared) {
            return false;
        }
    }
    return true;
}

void LockManager::refresh_group_lock_mode(LockRequestQueue& queue) {
    bool has_x = false;
    bool has_s = false;
    bool has_ix = false;
    bool has_is = false;
    for (const auto& request : queue.request_queue_) {
        if (!request.granted_) {
            continue;
        }
        has_x = has_x || request.lock_mode_ == LockMode::EXLUCSIVE;
        has_s = has_s || request.lock_mode_ == LockMode::SHARED;
        has_ix = has_ix || request.lock_mode_ == LockMode::INTENTION_EXCLUSIVE || request.lock_mode_ == LockMode::S_IX;
        has_is = has_is || request.lock_mode_ == LockMode::INTENTION_SHARED;
    }
    if (has_x) {
        queue.group_lock_mode_ = GroupLockMode::X;
    } else if (has_s && has_ix) {
        queue.group_lock_mode_ = GroupLockMode::SIX;
    } else if (has_s) {
        queue.group_lock_mode_ = GroupLockMode::S;
    } else if (has_ix) {
        queue.group_lock_mode_ = GroupLockMode::IX;
    } else if (has_is) {
        queue.group_lock_mode_ = GroupLockMode::IS;
    } else {
        queue.group_lock_mode_ = GroupLockMode::NON_LOCK;
    }
}

void LockManager::remove_stale_requests(LockRequestQueue& queue) {
    queue.request_queue_.remove_if([](const LockRequest& request) {
        auto iter = TransactionManager::txn_map.find(request.txn_id_);
        if (iter == TransactionManager::txn_map.end()) {
            return true;
        }
        auto state = iter->second->get_state();
        return state == TransactionState::COMMITTED || state == TransactionState::ABORTED;
    });
    refresh_group_lock_mode(queue);
}

bool LockManager::lock(Transaction* txn, LockDataId lock_data_id, LockMode lock_mode) {
    if (txn == nullptr) {
        return true;
    }
    std::unique_lock<std::mutex> guard(latch_);
    auto& queue = lock_table_[lock_data_id];
    remove_stale_requests(queue);
    auto existing = std::find_if(queue.request_queue_.begin(), queue.request_queue_.end(),
                                 [&](const LockRequest& request) {
                                     return request.txn_id_ == txn->get_transaction_id() && request.granted_;
                                 });

    if (existing != queue.request_queue_.end()) {
        if (existing->lock_mode_ == lock_mode || existing->lock_mode_ == LockMode::EXLUCSIVE) {
            return true;
        }
        if (existing->lock_mode_ == LockMode::SHARED && lock_mode == LockMode::EXLUCSIVE) {
            if (!compatible(queue, txn->get_transaction_id(), lock_mode)) {
                throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
            }
            existing->lock_mode_ = LockMode::EXLUCSIVE;
            refresh_group_lock_mode(queue);
            return true;
        }
        return true;
    }

    if (txn->get_state() == TransactionState::SHRINKING) {
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::LOCK_ON_SHIRINKING);
    }
    if (!compatible(queue, txn->get_transaction_id(), lock_mode)) {
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
    }

    queue.request_queue_.emplace_back(txn->get_transaction_id(), lock_mode);
    queue.request_queue_.back().granted_ = true;
    txn->get_lock_set()->insert(lock_data_id);
    refresh_group_lock_mode(queue);
    return true;
}

bool LockManager::lock_shared_on_record(Transaction* txn, const Rid& rid, int tab_fd) {
    return lock(txn, LockDataId(tab_fd, rid, LockDataType::RECORD), LockMode::SHARED);
}

bool LockManager::lock_exclusive_on_record(Transaction* txn, const Rid& rid, int tab_fd) {
    return lock(txn, LockDataId(tab_fd, rid, LockDataType::RECORD), LockMode::EXLUCSIVE);
}

bool LockManager::lock_shared_on_table(Transaction* txn, int tab_fd) {
    return lock(txn, LockDataId(tab_fd, LockDataType::TABLE), LockMode::SHARED);
}

bool LockManager::lock_exclusive_on_table(Transaction* txn, int tab_fd) {
    return lock(txn, LockDataId(tab_fd, LockDataType::TABLE), LockMode::EXLUCSIVE);
}

bool LockManager::lock_IS_on_table(Transaction* txn, int tab_fd) {
    return lock(txn, LockDataId(tab_fd, LockDataType::TABLE), LockMode::INTENTION_SHARED);
}

bool LockManager::lock_IX_on_table(Transaction* txn, int tab_fd) {
    return lock(txn, LockDataId(tab_fd, LockDataType::TABLE), LockMode::INTENTION_EXCLUSIVE);
}

bool LockManager::unlock(Transaction* txn, LockDataId lock_data_id) {
    if (txn == nullptr) {
        return true;
    }
    std::unique_lock<std::mutex> guard(latch_);
    auto queue_iter = lock_table_.find(lock_data_id);
    if (queue_iter == lock_table_.end()) {
        return false;
    }
    auto& queue = queue_iter->second;
    auto request_iter = std::find_if(queue.request_queue_.begin(), queue.request_queue_.end(),
                                     [&](const LockRequest& request) {
                                         return request.txn_id_ == txn->get_transaction_id();
                                     });
    if (request_iter == queue.request_queue_.end()) {
        return false;
    }

    queue.request_queue_.erase(request_iter);
    txn->get_lock_set()->erase(lock_data_id);
    if (txn->get_state() == TransactionState::GROWING) {
        txn->set_state(TransactionState::SHRINKING);
    }
    if (queue.request_queue_.empty()) {
        lock_table_.erase(queue_iter);
    } else {
        refresh_group_lock_mode(queue);
    }
    return true;
}
