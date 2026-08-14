#pragma once

#include <cassert>
#include <variant>
#include <optional>
#include <vector>
#include <iostream>
#include "arena.hpp"
#include "tokenization.hpp"

// AST узлы

struct NodeExpr;
struct NodeTerm;
struct NodeScope;
struct NodeStmt;

// Литералы
struct NodeTermIntLit { long long value; };
struct NodeTermShortLit { long long value; };
struct NodeTermLongLit { long long value; };
struct NodeTermLongLongLit { __int128 value; };
struct NodeTermFloatLit { double value; };
struct NodeTermDoubleLit { double value; };
struct NodeTermBoolLit { bool value; };
struct NodeTermCharLit { char value; };
struct NodeTermStrLit { std::string value; };
struct NodeTermIdent { Token ident; };
struct NodeTermParen { NodeExpr* expr; };
struct NodeTermType { TokenType type_tok; };
struct NodeTermRank { NodeExpr* expr; };
struct NodeTermSizeof { NodeExpr* expr; };
struct NodeTermToString { NodeExpr* expr; };

struct NodeTermCast { TokenType target_type; NodeExpr* expr; int ptr_level = 0; };

struct NodeTermMud {};

struct NodeTermAddrOf { Token ident; };

struct NodeTermDeref { NodeExpr* expr; };

struct NodeCall {
    Token ident;
    std::vector<NodeExpr*> args;
};

// Бинарные выражения
struct NodeBinExprAdd { NodeExpr* lhs; NodeExpr* rhs; };
struct NodeBinExprMulti { NodeExpr* lhs; NodeExpr* rhs; };
struct NodeBinExprSub { NodeExpr* lhs; NodeExpr* rhs; };
struct NodeBinExprDiv { NodeExpr* lhs; NodeExpr* rhs; };

// Операторы сравнения
struct NodeBinExprLess       { NodeExpr* lhs; NodeExpr* rhs; };  // <
struct NodeBinExprGreater    { NodeExpr* lhs; NodeExpr* rhs; };  // >
struct NodeBinExprEqual      { NodeExpr* lhs; NodeExpr* rhs; };  // <>
struct NodeBinExprNotEqual   { NodeExpr* lhs; NodeExpr* rhs; };  // ><
struct NodeBinExprLessEq     { NodeExpr* lhs; NodeExpr* rhs; };  // <<
struct NodeBinExprGreaterEq  { NodeExpr* lhs; NodeExpr* rhs; };  // >>

// Булевы операторы
struct NodeBinExprAnd { NodeExpr* lhs; NodeExpr* rhs; };  // and
struct NodeBinExprOr  { NodeExpr* lhs; NodeExpr* rhs; };  // or
struct NodeBinExprNor { NodeExpr* lhs; NodeExpr* rhs; };  // nor -> not (lhs or rhs)

struct NodeBinExpr {
    std::variant<
        NodeBinExprAdd*, NodeBinExprMulti*, NodeBinExprSub*, NodeBinExprDiv*,
        NodeBinExprLess*, NodeBinExprGreater*, NodeBinExprEqual*, NodeBinExprNotEqual*,
        NodeBinExprLessEq*, NodeBinExprGreaterEq*,
        NodeBinExprAnd*, NodeBinExprOr*, NodeBinExprNor*
    > var;
};

// Унарный оператор not
struct NodeUnaryExprNot { NodeExpr* expr; };

struct NodeTerm {
    std::variant<NodeTermIntLit*, NodeTermShortLit*, NodeTermLongLit*, NodeTermLongLongLit*, NodeTermFloatLit*, NodeTermDoubleLit*, NodeTermBoolLit*, NodeTermCharLit*, NodeTermStrLit*, NodeTermIdent*, NodeTermParen*, NodeCall*, NodeTermType*, NodeTermRank*, NodeTermSizeof*, NodeTermToString*, NodeTermCast*, NodeTermMud*, NodeTermAddrOf*, NodeTermDeref*> var;
};

struct NodeExpr {
    std::variant<NodeTerm*, NodeBinExpr*, NodeUnaryExprNot*> var;
};

// Операторы
struct NodeStmtWin { NodeExpr* expr; };

struct NodeStmtLet { Token ident; NodeExpr* expr; bool is_const; bool is_signed = false; int ptr_level = 0; };
struct NodeStmtAssign { Token ident; NodeExpr* expr; };
struct NodeStmtCut { Token ident; };
struct NodeStmtRadio { NodeExpr* expr; };

struct NodeStmtDerefAssign { NodeExpr* ptr_expr; NodeExpr* value_expr; };
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

struct NodeStmtWhile { NodeExpr* expr; NodeScope* scope; };
struct NodeStmtFor { NodeStmt* init; NodeExpr* cond; NodeStmt* update; NodeScope* scope; };

struct NodeStmt {
    std::variant<NodeStmtWin*, NodeStmtLet*, NodeStmtAssign*, NodeStmtCut*, NodeStmtRadio*, NodeScope*, NodeStmtIf*, NodeStmtWhile*, NodeStmtFor*, NodeStmtDerefAssign*> var;
};

// Функция
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

    static __int128 parse_int128(const std::string& s) {
        __int128 result = 0;
        for (char ch : s) {
            if (ch < '0' || ch > '9') break;
            result = result * 10 + (ch - '0');
        }
        return result;
    }

    static bool is_numeric_lit_type(TokenType t) {
        return t == TokenType::INT_LIT || t == TokenType::SHORT_LIT || t == TokenType::LONG_LIT ||
               t == TokenType::LONGLONG_LIT || t == TokenType::FLOAT_LIT || t == TokenType::DOUBLE_LIT;
    }

    static bool is_type_token(TokenType t) {
        switch (t) {
            case TokenType::TYPE_INT:
            case TokenType::TYPE_BOOL:
            case TokenType::TYPE_CHAR:
            case TokenType::TYPE_STRING:
            case TokenType::TYPE_FLOAT:
            case TokenType::TYPE_DOUBLE:
            case TokenType::TYPE_SHORT:
            case TokenType::TYPE_LONG:
            case TokenType::TYPE_LONGLONG:
                return true;
            default:
                return false;
        }
    }

    std::optional<NodeTerm*> parse_term() {

        if (try_consume(TokenType::STAR)) {
            auto inner = parse_term();
            if (!inner) error_expected("expression after '*' (dereference)");
            auto inner_expr = m_allocator.emplace<NodeExpr>(inner.value());
            auto deref = m_allocator.emplace<NodeTermDeref>(inner_expr);
            return m_allocator.emplace<NodeTerm>(deref);
        }
        if (try_consume(TokenType::AMPERSAND)) {
            auto ident = try_consume(TokenType::IDENT);
            if (!ident) error_expected("identifier after '&'");
            auto addr = m_allocator.emplace<NodeTermAddrOf>(ident.value());
            return m_allocator.emplace<NodeTerm>(addr);
        }
        bool negative = false;
        if (peek().has_value() && peek()->type == TokenType::MINUS &&
            peek(1).has_value() && is_numeric_lit_type(peek(1)->type)) {
            consume();
            negative = true;
        }
        if (auto int_lit = try_consume(TokenType::INT_LIT)) {
            const std::string& raw = int_lit->value.value();

            bool is_hex = raw.size() > 2 && raw[0] == '0' && (raw[1] == 'x' || raw[1] == 'X');
            long long val = is_hex ? std::stoll(raw, nullptr, 16) : std::stoll(raw);
            if (negative) val = -val;
            auto lit = m_allocator.emplace<NodeTermIntLit>(val);
            return m_allocator.emplace<NodeTerm>(lit);
        }
        if (auto short_lit = try_consume(TokenType::SHORT_LIT)) {
            long long val = std::stoll(short_lit->value.value());
            if (negative) val = -val;
            auto lit = m_allocator.emplace<NodeTermShortLit>(val);
            return m_allocator.emplace<NodeTerm>(lit);
        }
        if (auto long_lit = try_consume(TokenType::LONG_LIT)) {
            long long val = std::stoll(long_lit->value.value());
            if (negative) val = -val;
            auto lit = m_allocator.emplace<NodeTermLongLit>(val);
            return m_allocator.emplace<NodeTerm>(lit);
        }
        if (auto llong_lit = try_consume(TokenType::LONGLONG_LIT)) {
            __int128 val = parse_int128(llong_lit->value.value());
            if (negative) val = -val;
            auto lit = m_allocator.emplace<NodeTermLongLongLit>(val);
            return m_allocator.emplace<NodeTerm>(lit);
        }
        if (auto float_lit = try_consume(TokenType::FLOAT_LIT)) {
            double val = std::stod(float_lit->value.value());
            if (negative) val = -val;
            auto lit = m_allocator.emplace<NodeTermFloatLit>(val);
            return m_allocator.emplace<NodeTerm>(lit);
        }
        if (auto double_lit = try_consume(TokenType::DOUBLE_LIT)) {
            double val = std::stod(double_lit->value.value());
            if (negative) val = -val;
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
        if (try_consume(TokenType::MUD)) {
            auto mud = m_allocator.emplace<NodeTermMud>();
            return m_allocator.emplace<NodeTerm>(mud);
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
            auto lit = m_allocator.emplace<NodeTermIntLit>(1LL);
            return m_allocator.emplace<NodeTerm>(lit);
        }
        if (auto flee = try_consume(TokenType::FLEE)) {
            auto lit = m_allocator.emplace<NodeTermIntLit>(0LL);
            return m_allocator.emplace<NodeTerm>(lit);
        }
        if (auto cterr = try_consume(TokenType::CTERR)) {
            auto lit = m_allocator.emplace<NodeTermIntLit>(0LL);
            return m_allocator.emplace<NodeTerm>(lit);
        }
        if (auto terr = try_consume(TokenType::TERR)) {
            auto lit = m_allocator.emplace<NodeTermIntLit>(1LL);
            return m_allocator.emplace<NodeTerm>(lit);
        }

        if (peek().has_value() && peek()->type == TokenType::open_paren &&
            peek(1).has_value() && is_type_token(peek(1)->type)) {
            int star_count = 0;
            while (star_count < 2 && peek(2 + star_count).has_value() && peek(2 + star_count)->type == TokenType::STAR) {
                star_count++;
            }
            if (peek(2 + star_count).has_value() && peek(2 + star_count)->type == TokenType::close_paren) {
                consume();
                TokenType target_tok = consume().type;
                for (int i = 0; i < star_count; ++i) consume();
                consume();
                if (try_consume(TokenType::MUD)) {
                    auto cast = m_allocator.emplace<NodeTermCast>(target_tok, nullptr, star_count);
                    return m_allocator.emplace<NodeTerm>(cast);
                }
                auto operand = parse_term();
                if (!operand) error_expected("expression after type cast");
                auto operand_expr = m_allocator.emplace<NodeExpr>(operand.value());
                auto cast = m_allocator.emplace<NodeTermCast>(target_tok, operand_expr, star_count);
                return m_allocator.emplace<NodeTerm>(cast);
            }
        }
        if (try_consume(TokenType::open_paren)) {
            auto expr = parse_expr();
            if (!expr) error_expected("expression");
            try_consume_err(TokenType::close_paren);
            auto paren = m_allocator.emplace<NodeTermParen>(expr.value());
            return m_allocator.emplace<NodeTerm>(paren);
        }
        if (try_consume(TokenType::RANK)) {
            try_consume_err(TokenType::open_paren);
            auto expr = parse_expr();
            if (!expr) error_expected("expression");
            try_consume_err(TokenType::close_paren);
            auto rank = m_allocator.emplace<NodeTermRank>(expr.value());
            return m_allocator.emplace<NodeTerm>(rank);
        }
        if (try_consume(TokenType::SIZEOF)) {
            try_consume_err(TokenType::open_paren);
            auto expr = parse_expr();
            if (!expr) error_expected("expression");
            try_consume_err(TokenType::close_paren);
            auto sz = m_allocator.emplace<NodeTermSizeof>(expr.value());
            return m_allocator.emplace<NodeTerm>(sz);
        }

        if (peek().has_value() && peek()->type == TokenType::TYPE_STRING &&
            peek(1).has_value() && peek(1)->type == TokenType::open_paren) {
            consume();
            consume();
            auto expr = parse_expr();
            if (!expr) error_expected("expression");
            try_consume_err(TokenType::close_paren);
            auto conv = m_allocator.emplace<NodeTermToString>(expr.value());
            return m_allocator.emplace<NodeTerm>(conv);
        }
        for (TokenType tt : { TokenType::TYPE_INT, TokenType::TYPE_BOOL, TokenType::TYPE_CHAR,
                               TokenType::TYPE_STRING, TokenType::TYPE_FLOAT, TokenType::TYPE_DOUBLE,
                               TokenType::TYPE_SHORT, TokenType::TYPE_LONG, TokenType::TYPE_LONGLONG }) {
            if (auto type_tok = try_consume(tt)) {
                auto type_term = m_allocator.emplace<NodeTermType>(type_tok->type);
                return m_allocator.emplace<NodeTerm>(type_term);
            }
        }
        return {};
    }

    std::optional<NodeExpr*> parse_expr() {
        return parse_logic_or_expr();
    }

    std::optional<NodeExpr*> parse_logic_or_expr() {
        auto lhs = parse_logic_and_expr();
        if (!lhs) return {};
        auto expr_lhs = lhs.value();
        while (peek().has_value() && (peek()->type == TokenType::OR || peek()->type == TokenType::NOR)) {
            auto op = consume();
            auto rhs = parse_logic_and_expr();
            if (!rhs) error_expected("expression");
            auto bin = m_allocator.alloc<NodeBinExpr>();
            if (op.type == TokenType::OR) {
                bin->var = m_allocator.emplace<NodeBinExprOr>(expr_lhs, rhs.value());
            } else {
                bin->var = m_allocator.emplace<NodeBinExprNor>(expr_lhs, rhs.value());
            }
            expr_lhs = m_allocator.emplace<NodeExpr>(bin);
        }
        return expr_lhs;
    }

    std::optional<NodeExpr*> parse_logic_and_expr() {
        auto lhs = parse_logic_not_expr();
        if (!lhs) return {};
        auto expr_lhs = lhs.value();
        while (peek().has_value() && peek()->type == TokenType::AND) {
            consume();
            auto rhs = parse_logic_not_expr();
            if (!rhs) error_expected("expression");
            auto bin = m_allocator.alloc<NodeBinExpr>();
            bin->var = m_allocator.emplace<NodeBinExprAnd>(expr_lhs, rhs.value());
            expr_lhs = m_allocator.emplace<NodeExpr>(bin);
        }
        return expr_lhs;
    }

    std::optional<NodeExpr*> parse_logic_not_expr() {
        if (try_consume(TokenType::NOT)) {
            auto operand = parse_logic_not_expr();
            if (!operand) error_expected("expression");
            auto unary = m_allocator.emplace<NodeUnaryExprNot>(operand.value());
            return m_allocator.emplace<NodeExpr>(unary);
        }
        return parse_comparison_expr();
    }

    std::optional<NodeExpr*> parse_comparison_expr() {
        auto lhs = parse_arith_expr();
        if (!lhs) return {};
        auto expr_lhs = lhs.value();
        if (peek().has_value()) {
            TokenType t = peek()->type;
            if (t == TokenType::LESS || t == TokenType::GREATER || t == TokenType::EQUAL_CMP ||
                t == TokenType::NOT_EQUAL_CMP || t == TokenType::LESS_EQ || t == TokenType::GREATER_EQ) {
                auto op = consume();
                auto rhs = parse_arith_expr();
                if (!rhs) error_expected("expression");
                auto bin = m_allocator.alloc<NodeBinExpr>();
                switch (op.type) {
                    case TokenType::LESS:
                        bin->var = m_allocator.emplace<NodeBinExprLess>(expr_lhs, rhs.value()); break;
                    case TokenType::GREATER:
                        bin->var = m_allocator.emplace<NodeBinExprGreater>(expr_lhs, rhs.value()); break;
                    case TokenType::EQUAL_CMP:
                        bin->var = m_allocator.emplace<NodeBinExprEqual>(expr_lhs, rhs.value()); break;
                    case TokenType::NOT_EQUAL_CMP:
                        bin->var = m_allocator.emplace<NodeBinExprNotEqual>(expr_lhs, rhs.value()); break;
                    case TokenType::LESS_EQ:
                        bin->var = m_allocator.emplace<NodeBinExprLessEq>(expr_lhs, rhs.value()); break;
                    case TokenType::GREATER_EQ:
                        bin->var = m_allocator.emplace<NodeBinExprGreaterEq>(expr_lhs, rhs.value()); break;
                    default: assert(false);
                }
                expr_lhs = m_allocator.emplace<NodeExpr>(bin);
            }
        }
        return expr_lhs;
    }

    std::optional<NodeExpr*> parse_arith_expr(int min_prec = 0) {
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
            auto expr_rhs = parse_arith_expr(next_min_prec);
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

        if (peek().has_value() && peek()->type == TokenType::STAR) {
            consume();

            auto base = parse_term();
            if (!base) error_expected("pointer expression after '*'");
            auto ptr_expr = m_allocator.emplace<NodeExpr>(base.value());
            try_consume_err(TokenType::EQUAL);
            auto val = parse_expr();
            if (!val) error_expected("expression");
            try_consume_err(TokenType::SEMI);
            auto stmt = m_allocator.alloc<NodeStmtDerefAssign>();
            stmt->ptr_expr = ptr_expr;
            stmt->value_expr = val.value();
            return m_allocator.emplace<NodeStmt>(stmt);
        }
        if (try_consume(TokenType::SIGN)) {
            bool is_pin;
            if (try_consume(TokenType::PIN)) {
                is_pin = true;
            } else {
                try_consume_err(TokenType::LET);
                is_pin = false;
            }
            int ptr_level = 0;
            while (ptr_level < 2 && try_consume(TokenType::STAR)) ptr_level++;
            auto ident = try_consume(TokenType::IDENT);
            if (!ident) error_expected("identifier");
            try_consume_err(TokenType::EQUAL);
            auto stmt = m_allocator.alloc<NodeStmtLet>();
            stmt->ident = ident.value();
            stmt->is_const = is_pin;
            stmt->is_signed = true;
            stmt->ptr_level = ptr_level;
            auto expr = parse_expr();
            if (!expr) error_expected("expression");
            stmt->expr = expr.value();
            try_consume_err(TokenType::SEMI);
            return m_allocator.emplace<NodeStmt>(stmt);
        }
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
            auto expr = parse_expr();
            if (!expr) error_expected("expression");
            try_consume_err(TokenType::close_paren);
            try_consume_err(TokenType::SEMI);
            auto stmt = m_allocator.alloc<NodeStmtRadio>();
            stmt->expr = expr.value();
            return m_allocator.emplace<NodeStmt>(stmt);
        }
        if (auto tok = try_consume(TokenType::LET)) {

            int ptr_level = 0;
            while (ptr_level < 2 && try_consume(TokenType::STAR)) ptr_level++;
            auto ident = try_consume(TokenType::IDENT);
            if (!ident) error_expected("identifier");
            try_consume_err(TokenType::EQUAL);
            auto stmt = m_allocator.alloc<NodeStmtLet>();
            stmt->ident = ident.value();
            stmt->is_const = false;
            stmt->ptr_level = ptr_level;
            auto expr = parse_expr();
            if (!expr) error_expected("expression");
            stmt->expr = expr.value();
            try_consume_err(TokenType::SEMI);
            return m_allocator.emplace<NodeStmt>(stmt);
        }
        if (auto tok = try_consume(TokenType::PIN)) {
            int ptr_level = 0;
            while (ptr_level < 2 && try_consume(TokenType::STAR)) ptr_level++;
            auto ident = try_consume(TokenType::IDENT);
            if (!ident) error_expected("identifier");
            try_consume_err(TokenType::EQUAL);
            auto stmt = m_allocator.alloc<NodeStmtLet>();
            stmt->ident = ident.value();
            stmt->is_const = true;
            stmt->ptr_level = ptr_level;
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
        if (try_consume(TokenType::WHILE)) {
            try_consume_err(TokenType::open_paren);
            auto stmt = m_allocator.alloc<NodeStmtWhile>();
            auto expr = parse_expr();
            if (!expr) error_expected("expression");
            stmt->expr = expr.value();
            try_consume_err(TokenType::close_paren);
            auto scope = parse_scope();
            if (!scope) error_expected("scope");
            stmt->scope = scope.value();
            return m_allocator.emplace<NodeStmt>(stmt);
        }

        if (try_consume(TokenType::FOR)) {
            try_consume_err(TokenType::open_paren);
            auto stmt = m_allocator.alloc<NodeStmtFor>();

            auto init_ident = try_consume(TokenType::IDENT);
            if (!init_ident) error_expected("identifier in for-loop initializer");
            try_consume_err(TokenType::EQUAL);
            auto init_expr = parse_expr();
            if (!init_expr) error_expected("expression in for-loop initializer");
            auto init = m_allocator.alloc<NodeStmtAssign>();
            init->ident = init_ident.value();
            init->expr = init_expr.value();
            stmt->init = m_allocator.emplace<NodeStmt>(init);
            try_consume_err(TokenType::SEMI);

            auto cond = parse_expr();
            if (!cond) error_expected("condition expression in for-loop");
            stmt->cond = cond.value();
            try_consume_err(TokenType::SEMI);

            auto update_ident = try_consume(TokenType::IDENT);
            if (!update_ident) error_expected("identifier in for-loop update");
            try_consume_err(TokenType::EQUAL);
            auto update_expr = parse_expr();
            if (!update_expr) error_expected("expression in for-loop update");
            auto update = m_allocator.alloc<NodeStmtAssign>();
            update->ident = update_ident.value();
            update->expr = update_expr.value();
            stmt->update = m_allocator.emplace<NodeStmt>(update);

            try_consume_err(TokenType::close_paren);
            auto scope = parse_scope();
            if (!scope) error_expected("scope");
            stmt->scope = scope.value();
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