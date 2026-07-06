/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "ix_scan.h"

/**
 * @brief 
 * @todo 加上读锁（需要使用缓冲池得到page）
 */
void IxScan::next() {
    if (is_end()) {
        return;
    }
    IxNodeHandle *node = ih_->fetch_node(iid_.page_no);
    if (!node->is_leaf_page() || iid_.slot_no < 0 || iid_.slot_no >= node->get_size()) {
        bpm_->unpin_page(node->get_page_id(), false);
        delete node;
        iid_ = end_;
        return;
    }
    // increment slot no
    iid_.slot_no++;
    if (iid_.page_no != ih_->file_hdr_->last_leaf_ && iid_.slot_no == node->get_size()) {
        // go to next leaf
        page_id_t next_leaf = node->get_next_leaf();
        if (next_leaf == iid_.page_no || next_leaf == IX_NO_PAGE || next_leaf == IX_LEAF_HEADER_PAGE) {
            iid_ = end_;
        } else {
            iid_.slot_no = 0;
            iid_.page_no = next_leaf;
        }
    }
    bpm_->unpin_page(node->get_page_id(), false);
    delete node;
}

Rid IxScan::rid() const {
    return ih_->get_rid(iid_);
}
