/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#include "planner.h"

#include <algorithm>
#include <limits>
#include <memory>

bool Planner::get_index_cols(std::string tab_name, std::vector<Condition> curr_conds,
                             std::vector<std::string> &index_col_names) {
    index_col_names.clear();
    const auto &tab = sm_manager_->db_.get_table(tab_name);
    size_t best_score = 0;
    size_t best_covered = 0;
    size_t best_width = 0;
    for (const auto &index : tab.indexes) {
        size_t score = 0;
        size_t covered = 0;
        for (const auto &col : index.cols) {
            bool has_cond = std::any_of(curr_conds.begin(), curr_conds.end(), [&](const Condition &candidate) {
                return candidate.is_rhs_val && candidate.lhs_col.tab_name == tab_name &&
                       candidate.lhs_col.col_name == col.name && candidate.op != OP_NE;
            });
            if (has_cond) {
                covered++;
            }
        }
        for (const auto &col : index.cols) {
            auto cond = std::find_if(curr_conds.begin(), curr_conds.end(), [&](const Condition &candidate) {
                return candidate.is_rhs_val && candidate.lhs_col.tab_name == tab_name &&
                       candidate.lhs_col.col_name == col.name && candidate.op != OP_NE;
            });
            if (cond == curr_conds.end()) {
                break;
            }
            score++;
            if (cond->op != OP_EQ) {
                break;
            }
        }
        if (score > best_score ||
            (score == best_score && covered > best_covered) ||
            (score == best_score && covered == best_covered && index.cols.size() > best_width)) {
            best_score = score;
            best_covered = covered;
            best_width = index.cols.size();
            index_col_names.clear();
            for (const auto &col : index.cols) {
                index_col_names.push_back(col.name);
            }
        }
    }
    return best_score > 0;
}

bool Planner::get_order_index_cols(std::string tab_name, const std::vector<TabCol> &order_cols,
                                  const std::vector<bool> &is_desc, std::vector<std::string> &index_col_names,
                                  bool &reverse_index_scan) {
    index_col_names.clear();
    reverse_index_scan = false;
    if (order_cols.empty()) {
        return false;
    }
    if (std::any_of(is_desc.begin(), is_desc.end(), [&](bool flag) { return flag != is_desc.front(); })) {
        return false;
    }

    const auto &tab = sm_manager_->db_.get_table(tab_name);
    bool reverse = !is_desc.empty() && is_desc.front();
    size_t best_width = std::numeric_limits<size_t>::max();
    for (const auto &index : tab.indexes) {
        if (index.cols.size() < order_cols.size()) {
            continue;
        }
        bool matched = true;
        for (size_t i = 0; i < order_cols.size(); i++) {
            if (index.cols[i].name != order_cols[i].col_name) {
                matched = false;
                break;
            }
        }
        if (!matched) {
            continue;
        }
        if (index.cols.size() < best_width) {
            best_width = index.cols.size();
            index_col_names.clear();
            for (const auto &col : index.cols) {
                index_col_names.push_back(col.name);
            }
            reverse_index_scan = reverse;
        }
    }
    return !index_col_names.empty();
}

std::shared_ptr<Query> Planner::logical_optimization(std::shared_ptr<Query> query, Context *context) {
    (void)context;
    return query;
}

std::shared_ptr<Plan> Planner::physical_optimization(std::shared_ptr<Query> query, Context *context) {
    (void)context;
    auto plan = make_one_rel(query);
    return generate_sort_plan(query, std::move(plan));
}

std::shared_ptr<Plan> Planner::make_one_rel(std::shared_ptr<Query> query) {
    const auto &tables = query->tables;
    if (tables.empty()) {
        throw InternalError("SELECT requires at least one table");
    }

    auto select = std::dynamic_pointer_cast<ast::SelectStmt>(query->parse);
    std::vector<TabCol> order_cols;
    std::vector<bool> order_desc;
    if (select != nullptr && select->has_sort && tables.size() == 1) {
        for (const auto &order : select->orders) {
            TabCol order_col{order->cols->tab_name, order->cols->col_name};
            if (order_col.tab_name.empty()) {
                order_col.tab_name = tables.front();
            }
            order_cols.push_back(order_col);
            order_desc.push_back(order->orderby_dir == ast::OrderBy_DESC);
        }
    }

    std::vector<std::shared_ptr<Plan>> scans;
    scans.reserve(tables.size());
    for (const auto &table : tables) {
        std::vector<Condition> local_conds;
        for (const auto &cond : query->conds) {
            if (cond.lhs_col.tab_name == table &&
                (cond.is_rhs_val || cond.rhs_col.tab_name == table)) {
                local_conds.push_back(cond);
            }
        }
        std::vector<std::string> index_cols;
        bool reverse_index_scan = false;
        bool order_satisfied = false;
        bool has_index = get_index_cols(table, local_conds, index_cols);
        if (select != nullptr && select->has_sort && tables.size() == 1) {
            std::vector<std::string> order_index_cols;
            bool order_reverse = false;
            if (get_order_index_cols(table, order_cols, order_desc, order_index_cols, order_reverse)) {
                if (!has_index) {
                    index_cols = std::move(order_index_cols);
                    reverse_index_scan = order_reverse;
                    order_satisfied = true;
                    has_index = true;
                } else if (index_cols == order_index_cols) {
                    reverse_index_scan = order_reverse;
                    order_satisfied = true;
                }
            }
        }
        PlanTag scan_tag = has_index ? T_IndexScan : T_SeqScan;
        scans.push_back(std::make_shared<ScanPlan>(
            scan_tag, sm_manager_, table, std::move(local_conds), std::move(index_cols), reverse_index_scan,
            order_satisfied));
    }

    std::shared_ptr<Plan> root = scans.front();
    std::vector<std::string> joined{tables.front()};
    for (size_t i = 1; i < tables.size(); i++) {
        std::vector<Condition> join_conds;
        for (const auto &cond : query->conds) {
            if (cond.is_rhs_val) {
                continue;
            }
            bool lhs_old = std::find(joined.begin(), joined.end(), cond.lhs_col.tab_name) != joined.end();
            bool rhs_old = std::find(joined.begin(), joined.end(), cond.rhs_col.tab_name) != joined.end();
            bool lhs_new = cond.lhs_col.tab_name == tables[i];
            bool rhs_new = cond.rhs_col.tab_name == tables[i];
            if ((lhs_old && rhs_new) || (rhs_old && lhs_new)) {
                join_conds.push_back(cond);
            }
        }
        root = std::make_shared<JoinPlan>(T_NestLoop, root, scans[i], std::move(join_conds));
        joined.push_back(tables[i]);
    }
    return root;
}

std::shared_ptr<Plan> Planner::generate_sort_plan(std::shared_ptr<Query> query,
                                                  std::shared_ptr<Plan> plan) {
    auto select = std::dynamic_pointer_cast<ast::SelectStmt>(query->parse);
    if (select == nullptr || (!select->has_sort && !select->has_limit)) {
        return plan;
    }

    std::vector<ColMeta> all_cols;
    for (const auto &table : query->tables) {
        const auto &cols = sm_manager_->db_.get_table(table).cols;
        all_cols.insert(all_cols.end(), cols.begin(), cols.end());
    }
    std::vector<TabCol> order_cols;
    std::vector<bool> is_desc;
    for (const auto &order : select->orders) {
        TabCol order_col{order->cols->tab_name, order->cols->col_name};
        if (order_col.tab_name.empty()) {
            std::string table_name;
            for (const auto &col : all_cols) {
                if (col.name == order_col.col_name) {
                    if (!table_name.empty()) {
                        throw AmbiguousColumnError(order_col.col_name);
                    }
                    table_name = col.tab_name;
                }
            }
            if (table_name.empty()) {
                throw ColumnNotFoundError(order_col.col_name);
            }
            order_col.tab_name = table_name;
        } else {
            auto found = std::find_if(all_cols.begin(), all_cols.end(), [&](const ColMeta &col) {
                return col.tab_name == order_col.tab_name && col.name == order_col.col_name;
            });
            if (found == all_cols.end()) {
                throw ColumnNotFoundError(order_col.tab_name + "." + order_col.col_name);
            }
        }
        order_cols.push_back(std::move(order_col));
        is_desc.push_back(order->orderby_dir == ast::OrderBy_DESC);
    }
    bool input_sorted = false;
    if (auto scan = std::dynamic_pointer_cast<ScanPlan>(plan)) {
        input_sorted = scan->order_satisfied_;
    }
    return std::make_shared<SortPlan>(
        T_Sort, std::move(plan), std::move(order_cols), std::move(is_desc), select->limit, input_sorted);
}

std::shared_ptr<Plan> Planner::generate_select_plan(std::shared_ptr<Query> query, Context *context) {
    query = logical_optimization(std::move(query), context);
    auto root = physical_optimization(query, context);
    if (!query->aggregates.empty()) {
        return std::make_shared<AggregatePlan>(std::move(root), query->aggregates);
    }
    return std::make_shared<ProjectionPlan>(T_Projection, std::move(root), query->cols);
}

std::shared_ptr<Plan> Planner::do_planner(std::shared_ptr<Query> query, Context *context) {
    if (auto x = std::dynamic_pointer_cast<ast::CreateTable>(query->parse)) {
        std::vector<ColDef> col_defs;
        for (const auto &field : x->fields) {
            auto col = std::dynamic_pointer_cast<ast::ColDef>(field);
            if (col == nullptr) {
                throw InternalError("Unexpected field type");
            }
            col_defs.push_back(
                ColDef{col->col_name, interp_sv_type(col->type_len->type), col->type_len->len});
        }
        return std::make_shared<DDLPlan>(
            T_CreateTable, x->tab_name, std::vector<std::string>(), std::move(col_defs));
    }
    if (auto x = std::dynamic_pointer_cast<ast::DropTable>(query->parse)) {
        return std::make_shared<DDLPlan>(
            T_DropTable, x->tab_name, std::vector<std::string>(), std::vector<ColDef>());
    }
    if (auto x = std::dynamic_pointer_cast<ast::CreateIndex>(query->parse)) {
        return std::make_shared<DDLPlan>(
            T_CreateIndex, x->tab_name, x->col_names, std::vector<ColDef>());
    }
    if (auto x = std::dynamic_pointer_cast<ast::DropIndex>(query->parse)) {
        return std::make_shared<DDLPlan>(
            T_DropIndex, x->tab_name, x->col_names, std::vector<ColDef>());
    }
    if (auto x = std::dynamic_pointer_cast<ast::InsertStmt>(query->parse)) {
        return std::make_shared<DMLPlan>(
            T_Insert, nullptr, x->tab_name, query->values,
            std::vector<Condition>(), std::vector<SetClause>());
    }
    if (auto x = std::dynamic_pointer_cast<ast::DeleteStmt>(query->parse)) {
        std::vector<std::string> index_cols;
        PlanTag scan_tag = get_index_cols(x->tab_name, query->conds, index_cols) ? T_IndexScan : T_SeqScan;
        auto scan = std::make_shared<ScanPlan>(
            scan_tag, sm_manager_, x->tab_name, query->conds, std::move(index_cols));
        return std::make_shared<DMLPlan>(
            T_Delete, std::move(scan), x->tab_name, std::vector<Value>(),
            query->conds, std::vector<SetClause>());
    }
    if (auto x = std::dynamic_pointer_cast<ast::UpdateStmt>(query->parse)) {
        std::vector<std::string> index_cols;
        PlanTag scan_tag = get_index_cols(x->tab_name, query->conds, index_cols) ? T_IndexScan : T_SeqScan;
        auto scan = std::make_shared<ScanPlan>(
            scan_tag, sm_manager_, x->tab_name, query->conds, std::move(index_cols));
        return std::make_shared<DMLPlan>(
            T_Update, std::move(scan), x->tab_name, std::vector<Value>(),
            query->conds, query->set_clauses);
    }
    if (std::dynamic_pointer_cast<ast::SelectStmt>(query->parse)) {
        auto projection = generate_select_plan(query, context);
        return std::make_shared<DMLPlan>(
            T_select, std::move(projection), std::string(), std::vector<Value>(),
            std::vector<Condition>(), std::vector<SetClause>());
    }
    throw InternalError("Unexpected AST root");
}
