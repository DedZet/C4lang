#pragma once

#include <cassert>
#include <variant>
#include <optional>
#include <vector>
#include <iostream>
#include "arena.hpp"
#include "tokenization.hpp"

struct NodeExpr;
struct NodeTerm;
struct NodeScope;
struct NodeStmt;

struct NodeTermIntLit { Token int_lit; };
struct NodeTermFloatLit { double value; };
struct NodeTermDoubleLit { double value; };
struct NodeTermBoolLit { bool value; };
struct NodeTermCharLit { char value; };
struct NodeTermStrLit { std::string value; };
struct NodeTermIdent { Token ident; };
struct NodeTermParen { NodeExpr* expr; };

struct NodeCall {
    Token ident;
    std::vector<NodeExpr*> args;
};


struct NodeBinExprAdd { NodeExpr* lhs; NodeExpr* rhs; };
struct NodeBinExprMulti { NodeExpr* lhs; NodeExpr* rhs; };
struct NodeBinExprSub { NodeExpr* lhs; NodeExpr* rhs; };
struct NodeBinExprDiv { NodeExpr* lhs; NodeExpr* rhs; };

struct NodeBinExpr {
    std::variant<NodeBinExprAdd*, NodeBinExprMulti*, NodeBinExprSub*, NodeBinExprDiv*> var;
};

struct NodeTerm {
    std::variant<NodeTermIntLit*, NodeTermFloatLit*, NodeTermDoubleLit*, NodeTermBoolLit*, NodeTermCharLit*, NodeTermStrLit*, NodeTermIdent*, NodeTermParen*, NodeCall*> var;
};

struct NodeExpr {
    std::variant<NodeTerm*, NodeBinExpr*> var;
};

struct NodeStmtWin { NodeExpr* expr; };
struct NodeStmtLet { Token ident; NodeExpr* expr; bool is_const; };
struct NodeStmtAssign { Token ident; NodeExpr* expr; };
struct NodeStmtCut { Token ident; };
struct NodeStmtRadio { std::string str_lit; };
struct NodeScope { std::vector<NodeStmt*> stmts; };

struct NodeIfPred;
struct NodeIfPredElif { NodeExpr* expr; NodeScope* scope; std::optional<NodeIfPred*> pred; };
struct NodeIfPredElse { NodeScope* scope; };
struct NodeIfPred { std::variant<NodeIfPredElif*, NodeIfPredElse*> var; };

struct NodeStmtIf {
    NodeExpr* expr;
    NodeScope* scope;
    std::optional<NodeIfPred*> pred;
};

struct NodeStmt {
    std::variant<NodeStmtWin*, NodeStmtLet*, NodeStmtAssign*, NodeStmtCut*, NodeStmtRadio*, NodeScope*, NodeStmtIf*> var;
};

struct NodeFunc {
    Token ident;
    std::vector<Token> params;
    NodeScope* body;
};

struct NodeProg {
    std::vector<NodeFunc*> funcs;
};


class Parser {
public:
    explicit Parser(std::vector<Token> tokens)
        : m_tokens(std::move(tokens)), m_allocator(1024 * 1024 * 4) {}

    void error_expected(const std::string& msg) const {
        std::cerr << "[Parse Error] Expected " << msg << " on line " << peek(-1).value().line << std::endl;
        exit(EXIT_FAILURE);
    }

    std::optional<NodeTerm*> parse_term() {
        if (auto int_lit = try_consume(TokenType::INT_LIT)) {
            auto lit = m_allocator.emplace<NodeTermIntLit>(int_lit.value());
            return m_allocator.emplace<NodeTerm>(lit);
        }
        if (auto float_lit = try_consume(TokenType::FLOAT_LIT)) {
            double val = std::stod(float_lit->value.value());
            auto lit = m_allocator.emplace<NodeTermFloatLit>(val);
            return m_allocator.emplace<NodeTerm>(lit);
        }
        if (auto double_lit = try_consume(TokenType::DOUBLE_LIT)) {
            double val = std::stod(double_lit->value.value());
            auto lit = m_allocator.emplace<NodeTermDoubleLit>(val);
            return m_allocator.emplace<NodeTerm>(lit);
        }
        if (auto bool_lit = try_consume(TokenType::TRUE)) {
            auto lit = m_allocator.emplace<NodeTermBoolLit>(true);
            return m_allocator.emplace<NodeTerm>(lit);
        }
        if (auto bool_lit = try_consume(TokenType::FALSE)) {
            auto lit = m_allocator.emplace<NodeTermBoolLit>(false);
            return m_allocator.emplace<NodeTerm>(lit);
        }
        if (auto char_lit = try_consume(TokenType::CHAR_LIT)) {
            char val = char_lit->value.value()[0];
            auto lit = m_allocator.emplace<NodeTermCharLit>(val);
            return m_allocator.emplace<NodeTerm>(lit);
        }
        if (auto str_lit = try_consume(TokenType::STRING_LIT)) {
            auto lit = m_allocator.emplace<NodeTermStrLit>(str_lit->value.value());
            return m_allocator.emplace<NodeTerm>(lit);
        }
        if (auto ident = try_consume(TokenType::IDENT)) {
            if (peek().has_value() && peek().value().type == TokenType::open_paren) {
                consume();
                std::vector<NodeExpr*> args;
                while (peek().has_value() && peek().value().type != TokenType::close_paren) {
                    auto expr = parse_expr();
                    if (!expr) error_expected("expression");
                    args.push_back(expr.value());
                    if (peek().has_value() && peek().value().type == TokenType::COMMA) {
                        consume();
                    } else {
                        break;
                    }
                }
                try_consume_err(TokenType::close_paren);
                auto call = m_allocator.alloc<NodeCall>();
                call->ident = ident.value();
                call->args = std::move(args);
                return m_allocator.emplace<NodeTerm>(call);
            } else {
                auto id = m_allocator.emplace<NodeTermIdent>(ident.value());
                return m_allocator.emplace<NodeTerm>(id);
            }
        }
        if (auto spotted = try_consume(TokenType::SPOTTED)) {
            Token fake = { TokenType::INT_LIT, spotted->line, std::string("1") };
            auto lit = m_allocator.emplace<NodeTermIntLit>(fake);
            return m_allocator.emplace<NodeTerm>(lit);
        }
        if (auto flee = try_consume(TokenType::FLEE)) {
            Token fake = { TokenType::INT_LIT, flee->line, std::string("0") };
            auto lit = m_allocator.emplace<NodeTermIntLit>(fake);
            return m_allocator.emplace<NodeTerm>(lit);
        }
        if (auto cterr = try_consume(TokenType::CTERR)) {
            Token fake = { TokenType::INT_LIT, cterr->line, std::string("0") };
            auto lit = m_allocator.emplace<NodeTermIntLit>(fake);
            return m_allocator.emplace<NodeTerm>(lit);
        }
        if (auto terr = try_consume(TokenType::TERR)) {
            Token fake = { TokenType::INT_LIT, terr->line, std::string("1") };
            auto lit = m_allocator.emplace<NodeTermIntLit>(fake);
            return m_allocator.emplace<NodeTerm>(lit);
        }
        if (try_consume(TokenType::open_paren)) {
            auto expr = parse_expr();
            if (!expr) error_expected("expression");
            try_consume_err(TokenType::close_paren);
            auto paren = m_allocator.emplace<NodeTermParen>(expr.value());
            return m_allocator.emplace<NodeTerm>(paren);
        }
        return {};
    }

    std::optional<NodeExpr*> parse_expr(int min_prec = 0) {
        auto term_lhs = parse_term();
        if (!term_lhs) return {};
        auto expr_lhs = m_allocator.emplace<NodeExpr>(term_lhs.value());

        while (true) {
            auto curr = peek();
            if (!curr) break;
            auto prec = bin_prec(curr->type);
            if (!prec.has_value() || prec.value() < min_prec) break;

            auto op = consume();
            int next_min_prec = prec.value() + 1;
            auto expr_rhs = parse_expr(next_min_prec);
            if (!expr_rhs) error_expected("expression");

            auto bin = m_allocator.alloc<NodeBinExpr>();
            auto lhs2 = m_allocator.emplace<NodeExpr>();
            lhs2->var = expr_lhs->var;

            switch (op.type) {
                case TokenType::PLUS: {
                    auto add = m_allocator.emplace<NodeBinExprAdd>(lhs2, expr_rhs.value());
                    bin->var = add; break;
                }
                case TokenType::STAR: {
                    auto mul = m_allocator.emplace<NodeBinExprMulti>(lhs2, expr_rhs.value());
                    bin->var = mul; break;
                }
                case TokenType::MINUS: {
                    auto sub = m_allocator.emplace<NodeBinExprSub>(lhs2, expr_rhs.value());
                    bin->var = sub; break;
                }
                case TokenType::FSLAH: {
                    auto div = m_allocator.emplace<NodeBinExprDiv>(lhs2, expr_rhs.value());
                    bin->var = div; break;
                }
                default: assert(false);
            }
            expr_lhs->var = bin;
        }
        return expr_lhs;
    }

    std::optional<NodeScope*> parse_scope() {
        if (!try_consume(TokenType::OPEN_CURLY)) return {};
        auto scope = m_allocator.emplace<NodeScope>();
        while (auto stmt = parse_stmt()) {
            scope->stmts.push_back(stmt.value());
        }
        try_consume_err(TokenType::CLOSE_CURLY);
        return scope;
    }

    std::optional<NodeIfPred*> parse_if_pred() {
        if (try_consume(TokenType::ELIF)) {
            try_consume_err(TokenType::open_paren);
            auto elif = m_allocator.alloc<NodeIfPredElif>();
            auto expr = parse_expr();
            if (!expr) error_expected("expression");
            elif->expr = expr.value();
            try_consume_err(TokenType::close_paren);
            auto scope = parse_scope();
            if (!scope) error_expected("scope");
            elif->scope = scope.value();
            elif->pred = parse_if_pred();
            return m_allocator.emplace<NodeIfPred>(elif);
        }
        if (try_consume(TokenType::ELSE)) {
            auto else_ = m_allocator.alloc<NodeIfPredElse>();
            auto scope = parse_scope();
            if (!scope) error_expected("scope");
            else_->scope = scope.value();
            return m_allocator.emplace<NodeIfPred>(else_);
        }
        return {};
    }

    std::optional<NodeStmt*> parse_stmt() {
        if (try_consume(TokenType::WIN)) {
            auto stmt = m_allocator.alloc<NodeStmtWin>();
            auto expr = parse_expr();
            if (!expr) error_expected("expression");
            stmt->expr = expr.value();
            try_consume_err(TokenType::SEMI);
            return m_allocator.emplace<NodeStmt>(stmt);
        }
        if (try_consume(TokenType::RADIO)) {
            try_consume_err(TokenType::open_paren);
            auto str_tok = try_consume(TokenType::STRING_LIT);
            if (!str_tok) error_expected("string literal");
            try_consume_err(TokenType::close_paren);
            try_consume_err(TokenType::SEMI);
            auto stmt = m_allocator.alloc<NodeStmtRadio>();
            stmt->str_lit = str_tok->value.value();
            return m_allocator.emplace<NodeStmt>(stmt);
        }
        if (auto tok = try_consume(TokenType::LET)) {
            auto ident = try_consume(TokenType::IDENT);
            if (!ident) error_expected("identifier");
            try_consume_err(TokenType::EQUAL);
            auto stmt = m_allocator.alloc<NodeStmtLet>();
            stmt->ident = ident.value();
            stmt->is_const = false;
            auto expr = parse_expr();
            if (!expr) error_expected("expression");
            stmt->expr = expr.value();
            try_consume_err(TokenType::SEMI);
            return m_allocator.emplace<NodeStmt>(stmt);
        }
        if (auto tok = try_consume(TokenType::PIN)) {
            auto ident = try_consume(TokenType::IDENT);
            if (!ident) error_expected("identifier");
            try_consume_err(TokenType::EQUAL);
            auto stmt = m_allocator.alloc<NodeStmtLet>();
            stmt->ident = ident.value();
            stmt->is_const = true;
            auto expr = parse_expr();
            if (!expr) error_expected("expression");
            stmt->expr = expr.value();
            try_consume_err(TokenType::SEMI);
            return m_allocator.emplace<NodeStmt>(stmt);
        }
        if (auto ident = try_consume(TokenType::IDENT)) {
            if (try_consume(TokenType::EQUAL)) {
                auto stmt = m_allocator.alloc<NodeStmtAssign>();
                stmt->ident = ident.value();
                auto expr = parse_expr();
                if (!expr) error_expected("expression");
                stmt->expr = expr.value();
                try_consume_err(TokenType::SEMI);
                return m_allocator.emplace<NodeStmt>(stmt);
            } else {
                error_expected("'=' or function call");
            }
        }
        if (try_consume(TokenType::CUT)) {
            auto ident = try_consume(TokenType::IDENT);
            if (!ident) error_expected("identifier");
            try_consume_err(TokenType::SEMI);
            auto stmt = m_allocator.alloc<NodeStmtCut>();
            stmt->ident = ident.value();
            return m_allocator.emplace<NodeStmt>(stmt);
        }
        if (peek().has_value() && peek().value().type == TokenType::OPEN_CURLY) {
            auto scope = parse_scope();
            if (!scope) error_expected("scope");
            return m_allocator.emplace<NodeStmt>(scope.value());
        }
        if (try_consume(TokenType::IF)) {
            try_consume_err(TokenType::open_paren);
            auto stmt = m_allocator.alloc<NodeStmtIf>();
            auto expr = parse_expr();
            if (!expr) error_expected("expression");
            stmt->expr = expr.value();
            try_consume_err(TokenType::close_paren);
            auto scope = parse_scope();
            if (!scope) error_expected("scope");
            stmt->scope = scope.value();
            stmt->pred = parse_if_pred();
            return m_allocator.emplace<NodeStmt>(stmt);
        }
        return {};
    }

    std::optional<NodeFunc*> parse_func() {
        if (!try_consume(TokenType::BOMB)) return {};
        auto ident = try_consume(TokenType::IDENT);
        if (!ident) error_expected("function name");
        try_consume_err(TokenType::open_paren);
        std::vector<Token> params;
        while (peek().has_value() && peek().value().type != TokenType::close_paren) {
            auto param = try_consume(TokenType::IDENT);
            if (!param) error_expected("parameter name");
            params.push_back(param.value());
            if (peek().has_value() && peek().value().type == TokenType::COMMA) {
                consume();
            } else {
                break;
            }
        }
        try_consume_err(TokenType::close_paren);
        auto body = parse_scope();
        if (!body) error_expected("function body");
        auto func = m_allocator.alloc<NodeFunc>();
        func->ident = ident.value();
        func->params = std::move(params);
        func->body = body.value();
        return func;
    }

    std::optional<NodeProg> parse_prog() {
        NodeProg prog;
        while (peek().has_value()) {
            auto func = parse_func();
            if (func) {
                prog.funcs.push_back(func.value());
            } else {
                error_expected("function definition");
            }
        }
        bool has_plant = false;
        for (auto f : prog.funcs) {
            if (f->ident.value.value() == "plant") {
                has_plant = true;
                break;
            }
        }
        if (!has_plant) {
            std::cerr << "[Error] No 'plant' function defined (required entry point)" << std::endl;
            exit(EXIT_FAILURE);
        }
        return prog;
    }

private:
    std::optional<Token> peek(int offset = 0) const {
        if (m_index + offset >= m_tokens.size()) return {};
        return m_tokens.at(m_index + offset);
    }
    Token consume() { return m_tokens.at(m_index++); }
    Token try_consume_err(TokenType type) {
        if (peek().has_value() && peek().value().type == type)
            return consume();
        error_expected(to_string(type));
        return {};
    }
    std::optional<Token> try_consume(TokenType type) {
        if (peek().has_value() && peek().value().type == type)
            return consume();
        return {};
    }

    const std::vector<Token> m_tokens;
    size_t m_index = 0;
    ArenaAllocator m_allocator;
};