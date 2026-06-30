/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#include "analyze.h"

std::shared_ptr<Query> Analyze::do_analyze(std::shared_ptr<ast::TreeNode> parse) {
    auto query = std::make_shared<Query>();

    if (auto x = std::dynamic_pointer_cast<ast::SelectStmt>(parse)) {
        query->tables = x->tabs;
        for (const auto &table : query->tables) {
            sm_manager_->db_.get_table(table);
        }

        for (const auto &sv_col : x->cols) {
            query->cols.push_back(TabCol{sv_col->tab_name, sv_col->col_name});
        }

        std::vector<ColMeta> all_cols;
        get_all_cols(query->tables, all_cols);
        if (!x->aggregates.empty()) {
            for (const auto &sv_agg : x->aggregates) {
                AggregateSpec aggregate;
                aggregate.count_star = sv_agg->count_star;
                aggregate.alias = sv_agg->alias;
                switch (sv_agg->func) {
                    case ast::AGG_COUNT: aggregate.type = AGGREGATE_COUNT; break;
                    case ast::AGG_MAX: aggregate.type = AGGREGATE_MAX; break;
                    case ast::AGG_MIN: aggregate.type = AGGREGATE_MIN; break;
                    case ast::AGG_SUM: aggregate.type = AGGREGATE_SUM; break;
                }

                if (aggregate.count_star) {
                    aggregate.input_type = TYPE_INT;
                    aggregate.input_len = 0;
                    aggregate.output_type = TYPE_BIGINT;
                    aggregate.output_len = sizeof(int64_t);
                } else {
                    aggregate.col = check_column(
                        all_cols, TabCol{sv_agg->col->tab_name, sv_agg->col->col_name});
                    auto col = sm_manager_->db_.get_table(aggregate.col.tab_name)
                                   .get_col(aggregate.col.col_name);
                    aggregate.input_type = col->type;
                    aggregate.input_len = col->len;
                    if (aggregate.type == AGGREGATE_SUM &&
                        col->type != TYPE_INT && col->type != TYPE_FLOAT) {
                        throw IncompatibleTypeError("INT or FLOAT", coltype2str(col->type));
                    }
                    if ((aggregate.type == AGGREGATE_MAX || aggregate.type == AGGREGATE_MIN) &&
                        col->type != TYPE_INT && col->type != TYPE_FLOAT && col->type != TYPE_STRING) {
                        throw IncompatibleTypeError("INT, FLOAT or CHAR", coltype2str(col->type));
                    }
                    aggregate.output_type =
                        aggregate.type == AGGREGATE_COUNT ? TYPE_BIGINT :
                        aggregate.type == AGGREGATE_SUM && col->type == TYPE_INT ? TYPE_BIGINT :
                        col->type;
                    aggregate.output_len =
                        aggregate.output_type == TYPE_BIGINT ? sizeof(int64_t) : col->len;
                }
                query->aggregates.push_back(std::move(aggregate));
            }
        } else if (query->cols.empty()) {
            for (const auto &col : all_cols) {
                query->cols.push_back(TabCol{col.tab_name, col.name});
            }
        } else {
            for (auto &col : query->cols) {
                col = check_column(all_cols, col);
            }
        }

        get_clause(x->conds, query->conds);
        check_clause(query->tables, query->conds);
    } else if (auto x = std::dynamic_pointer_cast<ast::UpdateStmt>(parse)) {
        TabMeta &tab = sm_manager_->db_.get_table(x->tab_name);
        for (const auto &sv_set : x->set_clauses) {
            auto col = tab.get_col(sv_set->col_name);
            Value value = convert_sv_value(sv_set->val);
            if (col->type == TYPE_FLOAT && value.type == TYPE_INT) {
                value.set_float(static_cast<float>(value.int_val));
            } else if (col->type == TYPE_BIGINT && value.type == TYPE_INT) {
                value.set_bigint(value.int_val);
            } else if (col->type == TYPE_DATETIME && value.type == TYPE_STRING) {
                value.set_datetime(value.str_val);
            }
            if (col->type != value.type) {
                throw IncompatibleTypeError(coltype2str(col->type), coltype2str(value.type));
            }
            value.init_raw(col->len);
            query->set_clauses.push_back(
                SetClause{TabCol{x->tab_name, sv_set->col_name}, std::move(value)});
        }
        get_clause(x->conds, query->conds);
        check_clause({x->tab_name}, query->conds);
    } else if (auto x = std::dynamic_pointer_cast<ast::DeleteStmt>(parse)) {
        sm_manager_->db_.get_table(x->tab_name);
        get_clause(x->conds, query->conds);
        check_clause({x->tab_name}, query->conds);
    } else if (auto x = std::dynamic_pointer_cast<ast::InsertStmt>(parse)) {
        sm_manager_->db_.get_table(x->tab_name);
        for (const auto &sv_val : x->vals) {
            query->values.push_back(convert_sv_value(sv_val));
        }
    } else if (auto x = std::dynamic_pointer_cast<ast::DropTable>(parse)) {
        sm_manager_->db_.get_table(x->tab_name);
    } else if (auto x = std::dynamic_pointer_cast<ast::DescTable>(parse)) {
        sm_manager_->db_.get_table(x->tab_name);
    } else if (auto x = std::dynamic_pointer_cast<ast::ShowIndex>(parse)) {
        sm_manager_->db_.get_table(x->tab_name);
    }

    query->parse = std::move(parse);
    return query;
}

TabCol Analyze::check_column(const std::vector<ColMeta> &all_cols, TabCol target) {
    if (target.tab_name.empty()) {
        std::string table_name;
        for (const auto &col : all_cols) {
            if (col.name == target.col_name) {
                if (!table_name.empty()) {
                    throw AmbiguousColumnError(target.col_name);
                }
                table_name = col.tab_name;
            }
        }
        if (table_name.empty()) {
            throw ColumnNotFoundError(target.col_name);
        }
        target.tab_name = table_name;
    } else {
        auto pos = std::find_if(all_cols.begin(), all_cols.end(), [&](const ColMeta &col) {
            return col.tab_name == target.tab_name && col.name == target.col_name;
        });
        if (pos == all_cols.end()) {
            throw ColumnNotFoundError(target.tab_name + "." + target.col_name);
        }
    }
    return target;
}

void Analyze::get_all_cols(const std::vector<std::string> &tab_names, std::vector<ColMeta> &all_cols) {
    for (const auto &table_name : tab_names) {
        const auto &cols = sm_manager_->db_.get_table(table_name).cols;
        all_cols.insert(all_cols.end(), cols.begin(), cols.end());
    }
}

void Analyze::get_clause(const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds,
                         std::vector<Condition> &conds) {
    conds.clear();
    for (const auto &expr : sv_conds) {
        Condition cond;
        cond.lhs_col = TabCol{expr->lhs->tab_name, expr->lhs->col_name};
        cond.op = convert_sv_comp_op(expr->op);
        if (auto rhs_val = std::dynamic_pointer_cast<ast::Value>(expr->rhs)) {
            cond.is_rhs_val = true;
            cond.rhs_val = convert_sv_value(rhs_val);
        } else if (auto rhs_col = std::dynamic_pointer_cast<ast::Col>(expr->rhs)) {
            cond.is_rhs_val = false;
            cond.rhs_col = TabCol{rhs_col->tab_name, rhs_col->col_name};
        } else {
            throw InternalError("Unexpected condition expression");
        }
        conds.push_back(std::move(cond));
    }
}

void Analyze::check_clause(const std::vector<std::string> &tab_names, std::vector<Condition> &conds) {
    std::vector<ColMeta> all_cols;
    get_all_cols(tab_names, all_cols);
    for (auto &cond : conds) {
        cond.lhs_col = check_column(all_cols, cond.lhs_col);
        auto lhs_col = sm_manager_->db_.get_table(cond.lhs_col.tab_name).get_col(cond.lhs_col.col_name);
        ColType rhs_type;
        if (cond.is_rhs_val) {
            if (lhs_col->type == TYPE_FLOAT && cond.rhs_val.type == TYPE_INT) {
                cond.rhs_val.set_float(static_cast<float>(cond.rhs_val.int_val));
            } else if (lhs_col->type == TYPE_BIGINT && cond.rhs_val.type == TYPE_INT) {
                cond.rhs_val.set_bigint(cond.rhs_val.int_val);
            } else if (lhs_col->type == TYPE_DATETIME && cond.rhs_val.type == TYPE_STRING) {
                cond.rhs_val.set_datetime(cond.rhs_val.str_val);
            }
            cond.rhs_val.init_raw(lhs_col->len);
            rhs_type = cond.rhs_val.type;
        } else {
            cond.rhs_col = check_column(all_cols, cond.rhs_col);
            auto rhs_col = sm_manager_->db_.get_table(cond.rhs_col.tab_name).get_col(cond.rhs_col.col_name);
            rhs_type = rhs_col->type;
        }
        if (lhs_col->type != rhs_type) {
            throw IncompatibleTypeError(coltype2str(lhs_col->type), coltype2str(rhs_type));
        }
    }
}

Value Analyze::convert_sv_value(const std::shared_ptr<ast::Value> &sv_val) {
    Value val;
    if (auto int_lit = std::dynamic_pointer_cast<ast::IntLit>(sv_val)) {
        if (int_lit->val >= std::numeric_limits<int>::min() &&
            int_lit->val <= std::numeric_limits<int>::max()) {
            val.set_int(static_cast<int>(int_lit->val));
        } else {
            val.set_bigint(int_lit->val);
        }
    } else if (auto float_lit = std::dynamic_pointer_cast<ast::FloatLit>(sv_val)) {
        val.set_float(float_lit->val);
    } else if (auto str_lit = std::dynamic_pointer_cast<ast::StringLit>(sv_val)) {
        val.set_str(str_lit->val);
    } else {
        throw InternalError("Unexpected value type");
    }
    return val;
}

CompOp Analyze::convert_sv_comp_op(ast::SvCompOp op) {
    std::map<ast::SvCompOp, CompOp> ops = {
        {ast::SV_OP_EQ, OP_EQ}, {ast::SV_OP_NE, OP_NE}, {ast::SV_OP_LT, OP_LT},
        {ast::SV_OP_GT, OP_GT}, {ast::SV_OP_LE, OP_LE}, {ast::SV_OP_GE, OP_GE},
    };
    return ops.at(op);
}
