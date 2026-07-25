#pragma once

#include <algorithm>
#include <cassert>
#include <sstream>
#include <vector>
#include <iostream>
#include <cstring>
#include <unordered_map>
#include "parser.hpp"

class Generator {
public:
    explicit Generator(const NodeProg& prog)
        : m_prog(prog) {}

    std::string gen_prog() {
        collect_strings(m_prog);

        m_output << "section .data\n";
        for (size_t i = 0; i < m_string_literals.size(); ++i) {
            m_output << "str_" << i << ": db \"" << m_string_literals[i] << "\", 0\n";
        }
        m_output << "\n";

        m_output << "section .text\n";
        for (const auto* func : m_prog.funcs) {
            gen_func(func);
        }

        m_output << "global _start\n";
        m_output << "_start:\n";
        m_output << "    call plant\n";
        m_output << "    mov rdi, rax\n";
        m_output << "    mov rax, 60\n";
        m_output << "    syscall\n";
        return m_output.str();
    }

private:
    void gen_func(const NodeFunc* func) {
        m_output << "    ;; function " << func->ident.value.value() << "\n";
        m_output << func->ident.value.value() << ":\n";
        m_output << "    push rbp\n";
        m_output << "    mov rbp, rsp\n";

        m_stack_size = 0;
        m_vars.clear();
        m_scopes.clear();
        m_param_offsets.clear();

        size_t param_count = func->params.size();
        for (size_t i = 0; i < param_count; ++i) {
            const Token& param = func->params[i];
            int offset = static_cast<int>((param_count - i + 1) * 8);
            m_param_offsets[param.value.value()] = offset;
        }

        std::string epilog_label = create_label();
        m_current_epilog = epilog_label;

        gen_scope(func->body);

        m_output << epilog_label << ":\n";
        m_output << "    mov rsp, rbp\n";
        m_output << "    pop rbp\n";
        m_output << "    ret\n";
        m_output << "    ;; /function\n\n";
    }

    void gen_term(const NodeTerm* term) {
        struct Visitor {
            Generator& gen;
            void operator()(const NodeTermIntLit* t) const {
                gen.m_output << "    mov rax, " << t->int_lit.value.value() << "\n";
                gen.push("rax");
            }
            void operator()(const NodeTermFloatLit* t) const {
                long long bits;
                memcpy(&bits, &t->value, sizeof(t->value));
                gen.m_output << "    mov rax, " << bits << "\n";
                gen.push("rax");
            }
            void operator()(const NodeTermDoubleLit* t) const {
                long long bits;
                memcpy(&bits, &t->value, sizeof(t->value));
                gen.m_output << "    mov rax, " << bits << "\n";
                gen.push("rax");
            }
            void operator()(const NodeTermBoolLit* t) const {
                gen.m_output << "    mov rax, " << (t->value ? 1 : 0) << "\n";
                gen.push("rax");
            }
            void operator()(const NodeTermCharLit* t) const {
                gen.m_output << "    mov rax, " << static_cast<int>(t->value) << "\n";
                gen.push("rax");
            }
            void operator()(const NodeTermStrLit* t) const {
                gen.m_output << "    mov rax, 0\n";
                gen.push("rax");
            }
            void operator()(const NodeTermIdent* t) const {
                const std::string& name = t->ident.value.value();
                auto it = std::ranges::find_if(gen.m_vars, [&](const Var& v){
                    return v.name == name && !v.deleted;
                });
                if (it != gen.m_vars.end()) {
                    size_t offset = (gen.m_stack_size - it->stack_loc - 1) * 8;
                    gen.m_output << "    mov rax, [rsp + " << offset << "]\n";
                    gen.push("rax");
                    return;
                }
                auto param_it = gen.m_param_offsets.find(name);
                if (param_it != gen.m_param_offsets.end()) {
                    int offset = param_it->second;
                    gen.m_output << "    mov rax, [rbp + " << offset << "]\n";
                    gen.push("rax");
                    return;
                }
                std::cerr << "Undeclared identifier: " << name << std::endl;
                exit(EXIT_FAILURE);
            }
            void operator()(const NodeTermParen* t) const {
                gen.gen_expr(t->expr);
            }
            void operator()(const NodeCall* call) const {
                for (auto* arg : call->args) {
                    gen.gen_expr(arg);
                }
                gen.m_output << "    call " << call->ident.value.value() << "\n";
                if (!call->args.empty()) {
                    gen.m_output << "    add rsp, " << call->args.size() * 8 << "\n";
                    gen.m_stack_size -= call->args.size();
                }
                gen.push("rax");
            }
        };
        std::visit(Visitor{*this}, term->var);
    }

    void gen_bin_expr(const NodeBinExpr* bin) {
        struct Visitor {
            Generator& gen;
            void operator()(const NodeBinExprAdd* add) const {
                gen.gen_expr(add->rhs);
                gen.gen_expr(add->lhs);
                gen.pop("rax");
                gen.pop("rbx");
                gen.m_output << "    add rax, rbx\n";
                gen.push("rax");
            }
            void operator()(const NodeBinExprSub* sub) const {
                gen.gen_expr(sub->rhs);
                gen.gen_expr(sub->lhs);
                gen.pop("rax");
                gen.pop("rbx");
                gen.m_output << "    sub rax, rbx\n";
                gen.push("rax");
            }
            void operator()(const NodeBinExprMulti* mul) const {
                gen.gen_expr(mul->rhs);
                gen.gen_expr(mul->lhs);
                gen.pop("rax");
                gen.pop("rbx");
                gen.m_output << "    mul rbx\n";
                gen.push("rax");
            }
            void operator()(const NodeBinExprDiv* div) const {
                gen.gen_expr(div->rhs);
                gen.gen_expr(div->lhs);
                gen.pop("rax");
                gen.pop("rbx");
                gen.m_output << "    xor rdx, rdx\n";
                gen.m_output << "    div rbx\n";
                gen.push("rax");
            }
        };
        std::visit(Visitor{*this}, bin->var);
    }

    void gen_expr(const NodeExpr* expr) {
        struct Visitor {
            Generator& gen;
            void operator()(const NodeTerm* term) const { gen.gen_term(term); }
            void operator()(const NodeBinExpr* bin) const { gen.gen_bin_expr(bin); }
        };
        std::visit(Visitor{*this}, expr->var);
    }

    void gen_scope(const NodeScope* scope) {
        begin_scope();
        for (const auto* stmt : scope->stmts) {
            gen_stmt(stmt);
        }
        end_scope();
    }

    void gen_if_pred(const NodeIfPred* pred, const std::string& end_label) {
        struct Visitor {
            Generator& gen;
            const std::string& end_label;
            void operator()(const NodeIfPredElif* elif) const {
                gen.m_output << "    ;; elif\n";
                gen.gen_expr(elif->expr);
                gen.pop("rax");
                std::string label = gen.create_label();
                gen.m_output << "    test rax, rax\n";
                gen.m_output << "    jz " << label << "\n";
                gen.gen_scope(elif->scope);
                gen.m_output << "    jmp " << end_label << "\n";
                if (elif->pred.has_value()) {
                    gen.m_output << label << ":\n";
                    gen.gen_if_pred(elif->pred.value(), end_label);
                }
            }
            void operator()(const NodeIfPredElse* else_) const {
                gen.m_output << "    ;; else\n";
                gen.gen_scope(else_->scope);
            }
        };
        std::visit(Visitor{*this, end_label}, pred->var);
    }

    void gen_stmt(const NodeStmt* stmt) {
        struct Visitor {
            Generator& gen;
            void operator()(const NodeStmtWin* win) const {
                gen.gen_expr(win->expr);
                gen.pop("rax");
                gen.m_output << "    jmp " << gen.m_current_epilog << "\n";
            }
            void operator()(const NodeStmtRadio* radio) const {
                auto it = std::ranges::find(gen.m_string_literals, radio->str_lit);
                if (it == gen.m_string_literals.end()) {
                    std::cerr << "Internal error: string literal not found" << std::endl;
                    exit(EXIT_FAILURE);
                }
                size_t idx = std::distance(gen.m_string_literals.begin(), it);
                std::string label = "str_" + std::to_string(idx);
                gen.m_output << "    mov rax, 1\n";
                gen.m_output << "    mov rdi, 1\n";
                gen.m_output << "    mov rsi, " << label << "\n";
                gen.m_output << "    mov rdx, " << radio->str_lit.length() << "\n";
                gen.m_output << "    syscall\n";
            }
            void operator()(const NodeStmtLet* let) const {
                if (std::ranges::find_if(gen.m_vars, [&](const Var& v){
                    return v.name == let->ident.value.value() && !v.deleted;
                }) != gen.m_vars.end()) {
                    std::cerr << "Identifier already used: " << let->ident.value.value() << std::endl;
                    exit(EXIT_FAILURE);
                }
                gen.m_vars.push_back({ let->ident.value.value(), gen.m_stack_size, false, let->is_const });
                gen.gen_expr(let->expr);
            }
            void operator()(const NodeStmtAssign* assign) const {
                auto it = std::ranges::find_if(gen.m_vars, [&](const Var& v){
                    return v.name == assign->ident.value.value() && !v.deleted;
                });
                if (it == gen.m_vars.end()) {
                    std::cerr << "Undeclared identifier: " << assign->ident.value.value() << std::endl;
                    exit(EXIT_FAILURE);
                }
                if (it->is_const) {
                    std::cerr << "Cannot assign to constant variable: " << assign->ident.value.value() << std::endl;
                    exit(EXIT_FAILURE);
                }
                gen.gen_expr(assign->expr);
                gen.pop("rax");
                size_t offset = (gen.m_stack_size - it->stack_loc - 1) * 8;
                gen.m_output << "    mov [rsp + " << offset << "], rax\n";
            }
            void operator()(const NodeStmtCut* cut) const {
                auto it = std::ranges::find_if(gen.m_vars, [&](const Var& v){
                    return v.name == cut->ident.value.value() && !v.deleted;
                });
                if (it == gen.m_vars.end()) {
                    std::cerr << "Cannot cut undeclared identifier: " << cut->ident.value.value() << std::endl;
                    exit(EXIT_FAILURE);
                }
                it->deleted = true;
            }
            void operator()(const NodeScope* scope) const {
                gen.gen_scope(scope);
            }
            void operator()(const NodeStmtIf* if_) const {
                gen.m_output << "    ;; if\n";
                gen.gen_expr(if_->expr);
                gen.pop("rax");
                std::string label = gen.create_label();
                gen.m_output << "    test rax, rax\n";
                gen.m_output << "    jz " << label << "\n";
                gen.gen_scope(if_->scope);
                if (if_->pred.has_value()) {
                    std::string end_label = gen.create_label();
                    gen.m_output << "    jmp " << end_label << "\n";
                    gen.m_output << label << ":\n";
                    gen.gen_if_pred(if_->pred.value(), end_label);
                    gen.m_output << end_label << ":\n";
                } else {
                    gen.m_output << label << ":\n";
                }
                gen.m_output << "    ;; /if\n";
            }
        };
        std::visit(Visitor{*this}, stmt->var);
    }

    void collect_strings(const NodeProg& prog) {
        for (const auto* func : prog.funcs) {
            collect_strings_scope(func->body);
        }
    }

    void collect_strings_scope(const NodeScope* scope) {
        for (const auto* stmt : scope->stmts) {
            collect_strings_stmt(stmt);
        }
    }

    void collect_strings_stmt(const NodeStmt* stmt) {
        struct Visitor {
            Generator& gen;
            void operator()(const NodeStmtRadio* radio) {
                if (std::ranges::find(gen.m_string_literals, radio->str_lit) == gen.m_string_literals.end()) {
                    gen.m_string_literals.push_back(radio->str_lit);
                }
            }
            void operator()(const NodeScope* scope) {
                gen.collect_strings_scope(scope);
            }
            void operator()(const NodeStmtIf* if_) {
                gen.collect_strings_scope(if_->scope);
                if (if_->pred.has_value()) {
                    gen.collect_strings_pred(if_->pred.value());
                }
            }
            void operator()(const NodeStmtWin*) {}
            void operator()(const NodeStmtLet*) {}
            void operator()(const NodeStmtAssign*) {}
            void operator()(const NodeStmtCut*) {}
        };
        std::visit(Visitor{*this}, stmt->var);
    }

    void collect_strings_pred(const NodeIfPred* pred) {
        struct Visitor {
            Generator& gen;
            void operator()(const NodeIfPredElif* elif) {
                gen.collect_strings_scope(elif->scope);
                if (elif->pred.has_value()) {
                    gen.collect_strings_pred(elif->pred.value());
                }
            }
            void operator()(const NodeIfPredElse* else_) {
                gen.collect_strings_scope(else_->scope);
            }
        };
        std::visit(Visitor{*this}, pred->var);
    }

    void push(const std::string& reg) {
        m_output << "    push " << reg << "\n";
        m_stack_size++;
    }
    void pop(const std::string& reg) {
        m_output << "    pop " << reg << "\n";
        m_stack_size--;
    }

    void begin_scope() {
        m_scopes.push_back(m_vars.size());
    }
    void end_scope() {
        size_t pop_count = m_vars.size() - m_scopes.back();
        if (pop_count != 0) {
            m_output << "    add rsp, " << pop_count * 8 << "\n";
        }
        m_stack_size -= pop_count;
        for (size_t i = 0; i < pop_count; ++i) {
            m_vars.pop_back();
        }
        m_scopes.pop_back();
    }

    std::string create_label() {
        return "label" + std::to_string(m_label_count++);
    }

    struct Var {
        std::string name;
        size_t stack_loc;
        bool deleted = false;
        bool is_const = false;
    };

    const NodeProg& m_prog;
    std::stringstream m_output;
    size_t m_stack_size = 0;
    std::vector<Var> m_vars;
    std::vector<size_t> m_scopes;
    int m_label_count = 0;
    std::string m_current_epilog;
    std::unordered_map<std::string, int> m_param_offsets;
    std::vector<std::string> m_string_literals;
};