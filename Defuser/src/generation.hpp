#pragma once

#include <algorithm>
#include <cassert>
#include <sstream>
#include <vector>
#include <iostream>
#include <cstring>
#include <unordered_map>
#include "parser.hpp"

enum class ValueType : long long {
    INT = 0,
    BOOL = 1,
    CHAR = 2,
    STRING = 3,
    FLOAT = 4,
    DOUBLE = 5,
    SHORT = 6,      // new: 2 bytes
    LONG = 7,       // new: 8 bytes
    LONGLONG = 8,   // new: 16 bytes
    PTR = 9,        // new (pointers): любой указатель (адрес), 8 байт
    UNKNOWN = -1,
};

// (TYPE_INT/TYPE_BOOL/...) -> ValueType
inline ValueType type_from_token(TokenType t) {
    switch (t) {
        case TokenType::TYPE_INT:      return ValueType::INT;
        case TokenType::TYPE_BOOL:     return ValueType::BOOL;
        case TokenType::TYPE_CHAR:     return ValueType::CHAR;
        case TokenType::TYPE_STRING:   return ValueType::STRING;
        case TokenType::TYPE_FLOAT:    return ValueType::FLOAT;
        case TokenType::TYPE_DOUBLE:   return ValueType::DOUBLE;
        case TokenType::TYPE_SHORT:    return ValueType::SHORT;
        case TokenType::TYPE_LONG:     return ValueType::LONG;
        case TokenType::TYPE_LONGLONG: return ValueType::LONGLONG;
        default:                       return ValueType::UNKNOWN;
    }
}

// неоднозначные приведения
// Такие касты не переводят значение по правилам C, а возвращают MUD (NULL)
// как явно указано для (bool) char в задании, плюс, по аналогии, любые касты
// со string() кроме string -> string (для string используется отдельная
// функция string(x), поэтому C-style cast со string трактуется как
// неоднозначный, а не как reinterpret указателя)
inline bool is_ambiguous_cast(ValueType source, ValueType target) {
    if (source == ValueType::CHAR && target == ValueType::BOOL) return true;
    if (source == ValueType::STRING && target != ValueType::STRING) return true;
    if (target == ValueType::STRING && source != ValueType::STRING) return true;
    return false;
}

inline long long type_code(ValueType t) {
    if (t == ValueType::UNKNOWN) return static_cast<long long>(ValueType::INT); // лучшее приближение
    return static_cast<long long>(t);
}

inline int type_size_bytes(ValueType t) {
    switch (t) {
        case ValueType::SHORT:    return 2;
        case ValueType::INT:      return 4;
        case ValueType::LONG:     return 8;
        case ValueType::LONGLONG: return 16;
        case ValueType::FLOAT:    return 4;
        case ValueType::DOUBLE:   return 8;
        case ValueType::BOOL:     return 1;
        case ValueType::CHAR:     return 1;
        case ValueType::STRING:   return 8;
        case ValueType::PTR:      return 8;
        default:                  return 4; // UNKNOWN -> считаем как int
    }
}

inline unsigned __int128 unsigned_max_for_bytes(int bytes) {
    if (bytes >= 16) return ~(unsigned __int128)0;             // 2^128 - 1
    return (((unsigned __int128)1) << (bytes * 8)) - 1;        // 2^(bytes*8) - 1
}

// значение в беззнаковый диапазон
// Все числовые типы изначально unsigned. Если переменная объявлена БЕЗ `sign`
// и значение отрицательное, то оно преобразуется по формуле:
//   "конец интервала" - |значение|
// Например, для short (0..65535): -5  ->  65535 - 5 = 65530.
// Если переменная объявлена с `sign`, отрицательное значение остаётся как есть
// (обычное двоичное дополнение, которое естественно поддерживается регистром)
inline unsigned __int128 wrap_unsigned_value(__int128 value, ValueType t, bool is_signed) {
    if (is_signed || value >= 0) return static_cast<unsigned __int128>(value);
    unsigned __int128 max_val = unsigned_max_for_bytes(type_size_bytes(t));
    unsigned __int128 magnitude = static_cast<unsigned __int128>(-value);
    if (magnitude > max_val) magnitude = magnitude % (max_val + 1); // защита от переполнения диапазона
    return max_val - magnitude;
}

// перевод unsigned __int128 в десятичную строку
// std::ostream не умеет печатать __int128/unsigned __int128 напрямую
inline std::string u128_to_string(unsigned __int128 value) {
    if (value == 0) return "0";
    std::string s;
    while (value > 0) {
        s.push_back(static_cast<char>('0' + static_cast<int>(value % 10)));
        value /= 10;
    }
    std::ranges::reverse(s);
    return s;
}

// base_type:
//   * при ptr_level == 0 -- собственный тип значения (как раньше, ValueType);
//   * при ptr_level > 0  -- тип, НА КОТОРЫЙ указывает указатель в самом низу
//     цепочки разыменований (для T** это T, для T* это тоже T)
// ptr_level: 0 -- не указатель, 1 -- T*, 2 -- T**
struct FullType {
    ValueType base_type = ValueType::UNKNOWN;
    int ptr_level = 0;
    [[nodiscard]] ValueType effective_type() const {
        return ptr_level > 0 ? ValueType::PTR : base_type;
    }
};

class Generator {
public:
    explicit Generator(const NodeProg& prog)
        : m_prog(prog) {}

    std::string gen_prog() {
        collect_from_prog(m_prog);

        m_output << "section .data\n";
        for (size_t i = 0; i < m_string_literals.size(); ++i) {
            m_output << "str_" << i << ": db \"" << m_string_literals[i] << "\", 0\n";
        }
        m_output << "true_str: db \"True\", 0\n";
        m_output << "false_str: db \"False\", 0\n";
        m_output << "\n";

        if (m_conv_buffer_count > 0) {
            m_output << "section .bss\n";
            for (int i = 0; i < m_conv_buffer_count; ++i) {
                m_output << "strconv_" << i << ": resb 96\n";
            }
            m_output << "\n";
        }

        m_output << "section .bss\n";
        m_output << "heap_cur: resq 1\n";
        m_output << "heap_buf: resb " << HEAP_SIZE_BYTES << "\n";
        m_output << "\n";

        m_output << "section .text\n";
        for (const auto* func : m_prog.funcs) {
            gen_func(func);
        }

        m_output << "global _start\n";
        m_output << "_start:\n";
        m_output << "    mov rax, heap_buf\n";
        m_output << "    mov [heap_cur], rax\n";
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
            static unsigned long long low64_of(unsigned __int128 v) {
                unsigned __int128 mask = (((unsigned __int128)1) << 64) - 1;
                return static_cast<unsigned long long>(v & mask);
            }
            void emit_sized_int(__int128 raw_value, ValueType vt, const char* size_comment) const {
                unsigned __int128 wrapped = wrap_unsigned_value(raw_value, vt, gen.m_expect_signed);
                unsigned long long low64 = low64_of(wrapped);
                if (vt == ValueType::LONGLONG) {
                    gen.m_output << "    ;; long long (16 bytes) full value = " << u128_to_string(wrapped)
                                 << ", truncated to low 64 bits for rax (x86-64 has no 128-bit register)\n";
                }
                (void)size_comment;
                gen.m_output << "    mov rax, " << low64 << "\n";
                gen.push("rax");
            }
            void operator()(const NodeTermIntLit* t) const {
                emit_sized_int(t->value, ValueType::INT, "int, 4 bytes");
            }
            void operator()(const NodeTermShortLit* t) const {
                emit_sized_int(t->value, ValueType::SHORT, "short, 2 bytes");
            }
            void operator()(const NodeTermLongLit* t) const {
                emit_sized_int(t->value, ValueType::LONG, "long, 8 bytes");
            }
            void operator()(const NodeTermLongLongLit* t) const {
                emit_sized_int(t->value, ValueType::LONGLONG, "long long, 16 bytes");
            }
            void operator()(const NodeTermFloatLit* t) const {
                // битовое представление double
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

                auto it = std::ranges::find(gen.m_string_literals, t->value);
                if (it == gen.m_string_literals.end()) {
                    std::cerr << "Internal error: string literal not found" << std::endl;
                    exit(EXIT_FAILURE);
                }
                size_t idx = std::distance(gen.m_string_literals.begin(), it);
                gen.m_output << "    mov rax, str_" << idx << "\n";
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

                if (call->ident.value.value() == "malloc") {
                    if (call->args.size() != 1) {
                        std::cerr << "[Error] malloc() expects exactly 1 argument (size in bytes)" << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    gen.gen_expr(call->args[0]);
                    gen.pop("rax");
                    gen.m_output << "    ;; malloc(): bump allocator\n";
                    gen.m_output << "    mov rbx, [heap_cur]\n";
                    gen.m_output << "    mov rcx, rbx\n";     // rcx: адрес начала выделенного блока (результат)
                    gen.m_output << "    add rbx, rax\n";
                    gen.m_output << "    add rbx, 7\n";
                    gen.m_output << "    and rbx, -8\n";      // выравнивание следующего указателя по 8 байт
                    gen.m_output << "    mov [heap_cur], rbx\n";
                    gen.m_output << "    mov rax, rcx\n";
                    gen.push("rax");
                    return;
                }
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
            void operator()(const NodeTermType* t) const {
                ValueType vt;
                switch (t->type_tok) {
                    case TokenType::TYPE_INT:      vt = ValueType::INT; break;
                    case TokenType::TYPE_BOOL:     vt = ValueType::BOOL; break;
                    case TokenType::TYPE_CHAR:     vt = ValueType::CHAR; break;
                    case TokenType::TYPE_STRING:   vt = ValueType::STRING; break;
                    case TokenType::TYPE_FLOAT:    vt = ValueType::FLOAT; break;
                    case TokenType::TYPE_DOUBLE:   vt = ValueType::DOUBLE; break;
                    case TokenType::TYPE_SHORT:    vt = ValueType::SHORT; break;
                    case TokenType::TYPE_LONG:     vt = ValueType::LONG; break;
                    case TokenType::TYPE_LONGLONG: vt = ValueType::LONGLONG; break;
                    default: assert(false); vt = ValueType::UNKNOWN;
                }
                gen.m_output << "    mov rax, " << type_code(vt) << "\n";
                gen.push("rax");
            }
            void operator()(const NodeTermRank* r) const {
                ValueType vt = gen.infer_expr_type(r->expr);
                gen.m_output << "    mov rax, " << type_code(vt) << "\n";
                gen.push("rax");
            }
            void operator()(const NodeTermSizeof* s) const {
                ValueType vt = gen.infer_sizeof_type(s->expr);
                gen.m_output << "    mov rax, " << type_size_bytes(vt) << "\n";
                gen.push("rax");
            }
            void operator()(const NodeTermToString* c) const {
                gen.gen_to_string(c);
            }
            void operator()(const NodeTermCast* c) const {
                gen.gen_cast(c);
            }
            void operator()(const NodeTermMud*) const {
                std::cerr << "[Type Error] `MUD` used without an explicit type cast "
                             "(e.g. `(string) MUD`); a value/variable cannot be untyped" << std::endl;
                exit(EXIT_FAILURE);
            }
            void operator()(const NodeTermAddrOf* t) const {   // &ident адрес ячейки переменной
                const std::string& name = t->ident.value.value();
                auto it = std::ranges::find_if(gen.m_vars, [&](const Var& v){
                    return v.name == name && !v.deleted;
                });
                if (it != gen.m_vars.end()) {
                    size_t offset = (gen.m_stack_size - it->stack_loc - 1) * 8;
                    gen.m_output << "    lea rax, [rsp + " << offset << "]\n";
                    gen.push("rax");
                    return;
                }
                auto param_it = gen.m_param_offsets.find(name);
                if (param_it != gen.m_param_offsets.end()) {
                    int offset = param_it->second;
                    gen.m_output << "    lea rax, [rbp + " << offset << "]\n";
                    gen.push("rax");
                    return;
                }
                std::cerr << "Undeclared identifier: " << name << std::endl;
                exit(EXIT_FAILURE);
            }
            void operator()(const NodeTermDeref* d) const {   // *expr чтение через указатель
                FullType inner = gen.infer_expr_full_type(d->expr);
                if (inner.ptr_level < 1) {
                    std::cerr << "[Type Error] Cannot dereference a non-pointer value with '*'" << std::endl;
                    exit(EXIT_FAILURE);
                }
                gen.gen_expr(d->expr);
                gen.pop("rax"); // адрес
                if (inner.ptr_level > 1) {
                    // результат сам является указателем (адресом) -- 8 байт
                    gen.m_output << "    mov rax, [rax]\n";
                } else {
                    switch (type_size_bytes(inner.base_type)) {
                        case 1: gen.m_output << "    movzx rax, byte [rax]\n"; break;
                        case 2: gen.m_output << "    movzx rax, word [rax]\n"; break;
                        case 4: gen.m_output << "    mov eax, [rax]\n"; break; // обнуляет верхние 32 бита rax
                        default: gen.m_output << "    mov rax, [rax]\n"; break; // 8/16 байт -> грузит 8 байт
                    }
                }
                gen.push("rax");
            }
        };
        std::visit(Visitor{*this}, term->var);
    }


    void gen_cast(const NodeTermCast* node) {
 
        if (node->ptr_level > 0) {
            if (node->expr == nullptr) {
                m_output << "    ;; pointer cast of MUD -> null pointer\n";
                m_output << "    mov rax, 0\n";
                push("rax");
                return;
            }
            gen_expr(node->expr);
            return;
        }

        ValueType target = type_from_token(node->target_type);

        if (node->expr == nullptr) {
            // (type) MUD
            m_output << "    ;; (" << to_string(node->target_type) << ") MUD -> null\n";
            m_output << "    mov rax, 0\n";
            push("rax");
            return;
        }

        ValueType source = infer_expr_type(node->expr);

        if (target == ValueType::STRING && source != ValueType::STRING) {
            std::cerr << "[Type Error] Unsupported cast: `(string)` cannot be used to cast a non-string "
                          "value. Use `string(x)` to convert a value to its string representation instead "
                          "of `(string) x`." << std::endl;
            exit(EXIT_FAILURE);
        }

        gen_expr(node->expr);
        pop("rax");

        if (is_ambiguous_cast(source, target)) {
            m_output << "    ;; ambiguous cast to " << to_string(node->target_type) << " -> MUD\n";
            m_output << "    mov rax, 0\n";
            push("rax");
            return;
        }

        bool src_float = (source == ValueType::FLOAT || source == ValueType::DOUBLE);
        bool dst_float = (target == ValueType::FLOAT || target == ValueType::DOUBLE);

        if (src_float && dst_float) {
        } else if (src_float && !dst_float) {
            m_output << "    movq xmm0, rax\n";
            m_output << "    cvttsd2si rax, xmm0\n";
            if (target == ValueType::BOOL) {
                m_output << "    test rax, rax\n";
                m_output << "    setnz al\n";
                m_output << "    movzx rax, al\n";
            } else {
                apply_cast_truncation(target);
            }
        } else if (!src_float && dst_float) {
            m_output << "    cvtsi2sd xmm0, rax\n";
            m_output << "    movq rax, xmm0\n";
        } else {
            if (target == ValueType::BOOL) {
                m_output << "    test rax, rax\n";
                m_output << "    setnz al\n";
                m_output << "    movzx rax, al\n";
            } else {
                apply_cast_truncation(target);
            }
        }

        push("rax");
    }

    void apply_cast_truncation(ValueType target) {
        int bytes = type_size_bytes(target);
        if (bytes >= 8) return;
        if (bytes == 4) {

            m_output << "    mov eax, eax\n";
            return;
        }
        unsigned long long mask = (1ULL << (bytes * 8)) - 1;
        m_output << "    and rax, " << mask << "\n";
    }

    void gen_bin_expr(const NodeBinExpr* bin) {
        struct Visitor {
            Generator& gen;
            void operator()(const NodeBinExprAdd* add) const {

                FullType lft = gen.infer_expr_full_type(add->lhs);
                FullType rft = gen.infer_expr_full_type(add->rhs);
                gen.gen_expr(add->rhs);
                gen.gen_expr(add->lhs);
                gen.pop("rax"); // lhs
                gen.pop("rbx"); // rhs
                if (lft.ptr_level > 0 && rft.ptr_level == 0) {
                    int elem = type_size_bytes(lft.ptr_level > 1 ? ValueType::PTR : lft.base_type);
                    if (elem > 1) gen.m_output << "    imul rbx, " << elem << "\n";
                } else if (rft.ptr_level > 0 && lft.ptr_level == 0) {
                    int elem = type_size_bytes(rft.ptr_level > 1 ? ValueType::PTR : rft.base_type);
                    if (elem > 1) gen.m_output << "    imul rax, " << elem << "\n";
                }
                gen.m_output << "    add rax, rbx\n";
                gen.push("rax");
            }
            void operator()(const NodeBinExprSub* sub) const {

                FullType lft = gen.infer_expr_full_type(sub->lhs);
                FullType rft = gen.infer_expr_full_type(sub->rhs);
                gen.gen_expr(sub->rhs);
                gen.gen_expr(sub->lhs);
                gen.pop("rax");
                gen.pop("rbx");
                if (lft.ptr_level > 0 && rft.ptr_level == 0) {
                    int elem = type_size_bytes(lft.ptr_level > 1 ? ValueType::PTR : lft.base_type);
                    if (elem > 1) gen.m_output << "    imul rbx, " << elem << "\n";
                }
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
            void gen_cmp(const NodeExpr* lhs, const NodeExpr* rhs, const std::string& setcc) const {
                gen.gen_expr(rhs);
                gen.gen_expr(lhs);
                gen.pop("rax");   // lhs
                gen.pop("rbx");   // rhs
                gen.m_output << "    cmp rax, rbx\n";
                gen.m_output << "    " << setcc << " al\n";
                gen.m_output << "    movzx rax, al\n";
                gen.push("rax");
            }
            void operator()(const NodeBinExprLess* e) const { gen_cmp(e->lhs, e->rhs, "setl"); }
            void operator()(const NodeBinExprGreater* e) const { gen_cmp(e->lhs, e->rhs, "setg"); }
            void operator()(const NodeBinExprEqual* e) const { gen_cmp(e->lhs, e->rhs, "sete"); }
            void operator()(const NodeBinExprNotEqual* e) const { gen_cmp(e->lhs, e->rhs, "setne"); }
            void operator()(const NodeBinExprLessEq* e) const { gen_cmp(e->lhs, e->rhs, "setle"); }
            void operator()(const NodeBinExprGreaterEq* e) const { gen_cmp(e->lhs, e->rhs, "setge"); }

            void gen_bool_operands() const {
                gen.m_output << "    test rax, rax\n";
                gen.m_output << "    setnz al\n";
                gen.m_output << "    movzx rax, al\n";
                gen.m_output << "    test rbx, rbx\n";
                gen.m_output << "    setnz bl\n";
                gen.m_output << "    movzx rbx, bl\n";
            }
            void operator()(const NodeBinExprAnd* e) const {
                gen.gen_expr(e->rhs);
                gen.gen_expr(e->lhs);
                gen.pop("rax");
                gen.pop("rbx");
                gen_bool_operands();
                gen.m_output << "    and rax, rbx\n";
                gen.push("rax");
            }
            void operator()(const NodeBinExprOr* e) const {
                gen.gen_expr(e->rhs);
                gen.gen_expr(e->lhs);
                gen.pop("rax");
                gen.pop("rbx");
                gen_bool_operands();
                gen.m_output << "    or rax, rbx\n";
                gen.push("rax");
            }
            void operator()(const NodeBinExprNor* e) const {
                gen.gen_expr(e->rhs);
                gen.gen_expr(e->lhs);
                gen.pop("rax");
                gen.pop("rbx");
                gen_bool_operands();
                gen.m_output << "    or rax, rbx\n";
                gen.m_output << "    xor rax, 1\n"; // nor = not(or)
                gen.push("rax");
            }
        };
        std::visit(Visitor{*this}, bin->var);
    }


    ValueType infer_term_type(const NodeTerm* term) {
        return infer_term_full_type(term).effective_type();
    }

    ValueType infer_bin_expr_type(const NodeBinExpr* bin) {
        struct Visitor {
            Generator& gen;
            static int numeric_rank(ValueType t) {
                switch (t) {
                    case ValueType::DOUBLE:   return 5;
                    case ValueType::FLOAT:    return 4;
                    case ValueType::LONGLONG: return 3;
                    case ValueType::LONG:     return 2;
                    case ValueType::INT:      return 1;
                    case ValueType::SHORT:    return 0;
                    default:                  return 1;
                }
            }
            static ValueType numeric_combine(ValueType a, ValueType b) {
                return numeric_rank(a) >= numeric_rank(b) ? a : b;
            }
            ValueType operator()(const NodeBinExprAdd* e) const { return numeric_combine(gen.infer_expr_type(e->lhs), gen.infer_expr_type(e->rhs)); }
            ValueType operator()(const NodeBinExprSub* e) const { return numeric_combine(gen.infer_expr_type(e->lhs), gen.infer_expr_type(e->rhs)); }
            ValueType operator()(const NodeBinExprMulti* e) const { return numeric_combine(gen.infer_expr_type(e->lhs), gen.infer_expr_type(e->rhs)); }
            ValueType operator()(const NodeBinExprDiv* e) const { return numeric_combine(gen.infer_expr_type(e->lhs), gen.infer_expr_type(e->rhs)); }
            ValueType operator()(const NodeBinExprLess*) const { return ValueType::BOOL; }
            ValueType operator()(const NodeBinExprGreater*) const { return ValueType::BOOL; }
            ValueType operator()(const NodeBinExprEqual*) const { return ValueType::BOOL; }
            ValueType operator()(const NodeBinExprNotEqual*) const { return ValueType::BOOL; }
            ValueType operator()(const NodeBinExprLessEq*) const { return ValueType::BOOL; }
            ValueType operator()(const NodeBinExprGreaterEq*) const { return ValueType::BOOL; }
            ValueType operator()(const NodeBinExprAnd*) const { return ValueType::BOOL; }
            ValueType operator()(const NodeBinExprOr*) const { return ValueType::BOOL; }
            ValueType operator()(const NodeBinExprNor*) const { return ValueType::BOOL; }
        };
        return std::visit(Visitor{*this}, bin->var);
    }

    ValueType infer_expr_type(const NodeExpr* expr) {
        return infer_expr_full_type(expr).effective_type();
    }

    FullType infer_term_full_type(const NodeTerm* term) {
        struct Visitor {
            Generator& gen;
            FullType operator()(const NodeTermIntLit*) const { return { ValueType::INT, 0 }; }
            FullType operator()(const NodeTermShortLit*) const { return { ValueType::SHORT, 0 }; }
            FullType operator()(const NodeTermLongLit*) const { return { ValueType::LONG, 0 }; }
            FullType operator()(const NodeTermLongLongLit*) const { return { ValueType::LONGLONG, 0 }; }
            FullType operator()(const NodeTermFloatLit*) const { return { ValueType::FLOAT, 0 }; }
            FullType operator()(const NodeTermDoubleLit*) const { return { ValueType::DOUBLE, 0 }; }
            FullType operator()(const NodeTermBoolLit*) const { return { ValueType::BOOL, 0 }; }
            FullType operator()(const NodeTermCharLit*) const { return { ValueType::CHAR, 0 }; }
            FullType operator()(const NodeTermStrLit*) const { return { ValueType::STRING, 0 }; }
            FullType operator()(const NodeTermIdent* t) const {
                const std::string& name = t->ident.value.value();
                auto it = std::ranges::find_if(gen.m_vars, [&](const Var& v){
                    return v.name == name && !v.deleted;
                });
                if (it != gen.m_vars.end()) {
                    if (it->ptr_level > 0) return { it->base_type, it->ptr_level };
                    return { it->type, 0 };
                }
                return { ValueType::UNKNOWN, 0 }; // параметр функции - тип неизвестен статически
            }
            FullType operator()(const NodeTermParen* t) const { return gen.infer_expr_full_type(t->expr); }
            FullType operator()(const NodeCall*) const { return { ValueType::INT, 0 }; }
            FullType operator()(const NodeTermType*) const { return { ValueType::INT, 0 }; }
            FullType operator()(const NodeTermRank*) const { return { ValueType::INT, 0 }; }
            FullType operator()(const NodeTermSizeof*) const { return { ValueType::INT, 0 }; }
            FullType operator()(const NodeTermToString*) const { return { ValueType::STRING, 0 }; }
            FullType operator()(const NodeTermCast* c) const {
                return { type_from_token(c->target_type), c->ptr_level };
            }
            FullType operator()(const NodeTermMud*) const {
                std::cerr << "[Type Error] `MUD` used without an explicit type cast "
                             "(e.g. `(string) MUD`); a value/variable cannot be untyped" << std::endl;
                exit(EXIT_FAILURE);
                return { ValueType::UNKNOWN, 0 };
            }
            FullType operator()(const NodeTermAddrOf* t) const {
                const std::string& name = t->ident.value.value();
                auto it = std::ranges::find_if(gen.m_vars, [&](const Var& v){
                    return v.name == name && !v.deleted;
                });
                int new_level;
                ValueType base;
                if (it != gen.m_vars.end()) {
                    if (it->ptr_level > 0) { base = it->base_type; new_level = it->ptr_level + 1; }
                    else { base = it->type; new_level = 1; }
                } else {
                    base = ValueType::UNKNOWN; new_level = 1;
                }
                if (new_level > 2) {
                    std::cerr << "[Type Error] Only single and double pointers are supported (got &"
                               << name << " with indirection level " << new_level << ")" << std::endl;
                    exit(EXIT_FAILURE);
                }
                return { base, new_level };
            }
            FullType operator()(const NodeTermDeref* d) const {
                FullType inner = gen.infer_expr_full_type(d->expr);
                if (inner.ptr_level < 1) {
                    std::cerr << "[Type Error] Cannot dereference a non-pointer value with '*'" << std::endl;
                    exit(EXIT_FAILURE);
                }
                if (inner.ptr_level > 1) return { inner.base_type, inner.ptr_level - 1 };
                return { inner.base_type, 0 };
            }
        };
        return std::visit(Visitor{*this}, term->var);
    }

    FullType infer_bin_expr_full_type(const NodeBinExpr* bin) {
        struct Visitor {
            Generator& gen;
            static int numeric_rank(ValueType t) {
                switch (t) {
                    case ValueType::DOUBLE:   return 5;
                    case ValueType::FLOAT:    return 4;
                    case ValueType::LONGLONG: return 3;
                    case ValueType::LONG:     return 2;
                    case ValueType::INT:      return 1;
                    case ValueType::SHORT:    return 0;
                    default:                  return 1;
                }
            }
            static FullType numeric_combine(FullType a, FullType b) {
                return numeric_rank(a.base_type) >= numeric_rank(b.base_type) ? a : b;
            }

            FullType add_sub(const NodeExpr* lhs, const NodeExpr* rhs) const {
                FullType l = gen.infer_expr_full_type(lhs);
                FullType r = gen.infer_expr_full_type(rhs);
                if (l.ptr_level > 0 && r.ptr_level == 0) return l;
                if (r.ptr_level > 0 && l.ptr_level == 0) return r;
                return numeric_combine(l, r);
            }
            FullType operator()(const NodeBinExprAdd* e) const { return add_sub(e->lhs, e->rhs); }
            FullType operator()(const NodeBinExprSub* e) const { return add_sub(e->lhs, e->rhs); }
            FullType operator()(const NodeBinExprMulti* e) const { return numeric_combine(gen.infer_expr_full_type(e->lhs), gen.infer_expr_full_type(e->rhs)); }
            FullType operator()(const NodeBinExprDiv* e) const { return numeric_combine(gen.infer_expr_full_type(e->lhs), gen.infer_expr_full_type(e->rhs)); }
            FullType operator()(const NodeBinExprLess*) const { return { ValueType::BOOL, 0 }; }
            FullType operator()(const NodeBinExprGreater*) const { return { ValueType::BOOL, 0 }; }
            FullType operator()(const NodeBinExprEqual*) const { return { ValueType::BOOL, 0 }; }
            FullType operator()(const NodeBinExprNotEqual*) const { return { ValueType::BOOL, 0 }; }
            FullType operator()(const NodeBinExprLessEq*) const { return { ValueType::BOOL, 0 }; }
            FullType operator()(const NodeBinExprGreaterEq*) const { return { ValueType::BOOL, 0 }; }
            FullType operator()(const NodeBinExprAnd*) const { return { ValueType::BOOL, 0 }; }
            FullType operator()(const NodeBinExprOr*) const { return { ValueType::BOOL, 0 }; }
            FullType operator()(const NodeBinExprNor*) const { return { ValueType::BOOL, 0 }; }
        };
        return std::visit(Visitor{*this}, bin->var);
    }

    FullType infer_expr_full_type(const NodeExpr* expr) {
        struct Visitor {
            Generator& gen;
            FullType operator()(const NodeTerm* term) const { return gen.infer_term_full_type(term); }
            FullType operator()(const NodeBinExpr* bin) const { return gen.infer_bin_expr_full_type(bin); }
            FullType operator()(const NodeUnaryExprNot*) const { return { ValueType::BOOL, 0 }; }
        };
        return std::visit(Visitor{*this}, expr->var);
    }

    ValueType infer_sizeof_type(const NodeExpr* expr) {
        if (auto* term_ptr = std::get_if<NodeTerm*>(&expr->var)) {
            if (auto* type_ptr = std::get_if<NodeTermType*>(&(*term_ptr)->var)) {
                return type_from_token((*type_ptr)->type_tok);
            }
        }
        return infer_expr_type(expr);
    }

    void gen_expr(const NodeExpr* expr) {
        struct Visitor {
            Generator& gen;
            void operator()(const NodeTerm* term) const { gen.gen_term(term); }
            void operator()(const NodeBinExpr* bin) const { gen.gen_bin_expr(bin); }
            void operator()(const NodeUnaryExprNot* not_) const {
                gen.gen_expr(not_->expr);
                gen.pop("rax");
                gen.m_output << "    test rax, rax\n";
                gen.m_output << "    setz al\n";
                gen.m_output << "    movzx rax, al\n";
                gen.push("rax");
            }
        };
        std::visit(Visitor{*this}, expr->var);
    }

    void gen_to_string(const NodeTermToString* node) {
        ValueType vt = infer_expr_type(node->expr);
        gen_expr(node->expr);
        pop("rax");

        auto idx_it = m_conv_buffer_index.find(node);
        if (idx_it == m_conv_buffer_index.end()) {
            std::cerr << "Internal error: string() conversion site not found" << std::endl;
            exit(EXIT_FAILURE);
        }
        std::string buf = "strconv_" + std::to_string(idx_it->second);

        switch (vt) {
            case ValueType::STRING: {

                push("rax");
                break;
            }
            case ValueType::BOOL: {
                std::string else_label = create_label();
                std::string end_label = create_label();
                m_output << "    test rax, rax\n";
                m_output << "    jz " << else_label << "\n";
                m_output << "    mov rax, true_str\n";
                m_output << "    jmp " << end_label << "\n";
                m_output << else_label << ":\n";
                m_output << "    mov rax, false_str\n";
                m_output << end_label << ":\n";
                push("rax");
                break;
            }
            case ValueType::CHAR: {
                m_output << "    mov byte [" << buf << "], al\n";
                m_output << "    mov byte [" << buf << " + 1], 0\n";
                m_output << "    mov rax, " << buf << "\n";
                push("rax");
                break;
            }
            case ValueType::FLOAT:
            case ValueType::DOUBLE: {
                emit_float_to_string(buf);
                push("rax");
                break;
            }
            default: {
                emit_int_to_string(buf);
                push("rax");
                break;
            }
        }
    }

    void emit_int_to_string(const std::string& buf) {
        std::string skip_neg = create_label();
        std::string loop_label = create_label();
        std::string done_label = create_label();
        m_output << "    mov rcx, rax\n";
        m_output << "    lea rdi, [" << buf << " + 95]\n";
        m_output << "    mov byte [rdi], 0\n";
        m_output << "    dec rdi\n";
        m_output << "    mov rbx, 10\n";
        m_output << "    test rax, rax\n";
        m_output << "    jns " << skip_neg << "\n";
        m_output << "    neg rax\n";
        m_output << skip_neg << ":\n";
        m_output << loop_label << ":\n";
        m_output << "    xor rdx, rdx\n";
        m_output << "    div rbx\n";
        m_output << "    add dl, '0'\n";
        m_output << "    mov [rdi], dl\n";
        m_output << "    dec rdi\n";
        m_output << "    test rax, rax\n";
        m_output << "    jnz " << loop_label << "\n";
        m_output << "    test rcx, rcx\n";
        m_output << "    jns " << done_label << "\n";
        m_output << "    mov byte [rdi], '-'\n";
        m_output << "    dec rdi\n";
        m_output << done_label << ":\n";
        m_output << "    inc rdi\n";
        m_output << "    mov rax, rdi\n";
    }

    void emit_float_to_string(const std::string& buf) {
        std::string skip_neg = create_label();
        std::string int_loop = create_label();
        std::string copy_loop = create_label();
        std::string copy_done = create_label();
        std::string frac_loop = create_label();

        m_output << "    movq xmm0, rax\n";
        m_output << "    lea r12, [" << buf << "]\n";
        m_output << "    pxor xmm1, xmm1\n";
        m_output << "    comisd xmm0, xmm1\n";
        m_output << "    jae " << skip_neg << "\n";
        m_output << "    mov byte [r12], '-'\n";
        m_output << "    inc r12\n";
        m_output << "    xorpd xmm2, xmm2\n";
        m_output << "    subsd xmm2, xmm0\n";
        m_output << "    movsd xmm0, xmm2\n";
        m_output << skip_neg << ":\n";

        m_output << "    cvttsd2si rax, xmm0\n";
        m_output << "    cvtsi2sd xmm1, rax\n";
        m_output << "    subsd xmm0, xmm1\n";

        m_output << "    lea rdi, [" << buf << " + 95]\n";
        m_output << "    mov byte [rdi], 0\n";
        m_output << "    mov rbx, 10\n";
        m_output << int_loop << ":\n";
        m_output << "    xor rdx, rdx\n";
        m_output << "    div rbx\n";
        m_output << "    add dl, '0'\n";
        m_output << "    dec rdi\n";
        m_output << "    mov [rdi], dl\n";
        m_output << "    test rax, rax\n";
        m_output << "    jnz " << int_loop << "\n";
        m_output << copy_loop << ":\n";
        m_output << "    mov al, [rdi]\n";
        m_output << "    cmp al, 0\n";
        m_output << "    je " << copy_done << "\n";
        m_output << "    mov [r12], al\n";
        m_output << "    inc r12\n";
        m_output << "    inc rdi\n";
        m_output << "    jmp " << copy_loop << "\n";
        m_output << copy_done << ":\n";

        m_output << "    mov byte [r12], '.'\n";
        m_output << "    inc r12\n";

        m_output << "    mov rcx, 6\n";
        m_output << "    mov rax, 10\n";
        m_output << "    cvtsi2sd xmm3, rax\n";
        m_output << frac_loop << ":\n";
        m_output << "    mulsd xmm0, xmm3\n";
        m_output << "    cvttsd2si rax, xmm0\n";
        m_output << "    cvtsi2sd xmm1, rax\n";
        m_output << "    subsd xmm0, xmm1\n";
        m_output << "    add al, '0'\n";
        m_output << "    mov [r12], al\n";
        m_output << "    inc r12\n";
        m_output << "    dec rcx\n";
        m_output << "    jnz " << frac_loop << "\n";
        m_output << "    mov byte [r12], 0\n";

        m_output << "    mov rax, " << buf << "\n";
    }

    void emit_strlen() {
        std::string loop_label = create_label();
        std::string end_label = create_label();
        m_output << "    xor rdx, rdx\n";
        m_output << loop_label << ":\n";
        m_output << "    cmp byte [rsi + rdx], 0\n";
        m_output << "    je " << end_label << "\n";
        m_output << "    inc rdx\n";
        m_output << "    jmp " << loop_label << "\n";
        m_output << end_label << ":\n";
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

                ValueType vt = gen.infer_expr_type(radio->expr);
                if (vt != ValueType::STRING) {
                    std::cerr << "[Type Error] radio() expects a string argument; use string(...) to convert other types" << std::endl;
                    exit(EXIT_FAILURE);
                }
                gen.gen_expr(radio->expr);
                gen.pop("rax");
                gen.m_output << "    mov rsi, rax\n";
                gen.emit_strlen();     // -> rdx
                gen.m_output << "    mov rax, 1\n";
                gen.m_output << "    mov rdi, 1\n";
                gen.m_output << "    syscall\n";
            }
            void operator()(const NodeStmtLet* let) const {
                if (std::ranges::find_if(gen.m_vars, [&](const Var& v){
                    return v.name == let->ident.value.value() && !v.deleted;
                }) != gen.m_vars.end()) {

                    std::cerr << "[Type Error] Cannot redeclare '" << let->ident.value.value()
                               << "': variable redefinition (including changing its type) is not allowed. "
                               << "Use 'cut " << let->ident.value.value() << ";' first if you want to reuse the name." << std::endl;
                    exit(EXIT_FAILURE);
                }
                ValueType var_stored_type;
                int var_ptr_level = 0;
                ValueType var_base_type = ValueType::UNKNOWN;
                if (let->ptr_level > 0) {

                    bool is_bare_str_lit = false;
                    if (auto* term_ptr = std::get_if<NodeTerm*>(&let->expr->var)) {
                        if (std::holds_alternative<NodeTermStrLit*>((*term_ptr)->var)) is_bare_str_lit = true;
                    }
                    if (is_bare_str_lit && let->ptr_level == 1) {
                        var_base_type = ValueType::CHAR;
                        var_ptr_level = 1;
                    } else {
                        FullType ft = gen.infer_expr_full_type(let->expr);
                        if (ft.ptr_level == 0) {
                            std::cerr << "[Type Error] Cannot initialize pointer '" << let->ident.value.value()
                                       << "' with a non-pointer value" << std::endl;
                            exit(EXIT_FAILURE);
                        }
                        if (ft.ptr_level != let->ptr_level) {
                            std::cerr << "[Type Error] Pointer indirection level mismatch for '"
                                       << let->ident.value.value() << "' (declared with "
                                       << let->ptr_level << " '*', initializer has " << ft.ptr_level << ")" << std::endl;
                            exit(EXIT_FAILURE);
                        }
                        var_base_type = ft.base_type;
                        var_ptr_level = ft.ptr_level;
                    }
                    var_stored_type = ValueType::PTR;
                } else {
                    var_stored_type = gen.infer_expr_type(let->expr);
                }
                gen.m_vars.push_back({ let->ident.value.value(), gen.m_stack_size, false, let->is_const,
                                        var_stored_type, let->is_signed, var_ptr_level, var_base_type });

                bool prev_expect_signed = gen.m_expect_signed;
                gen.m_expect_signed = let->is_signed;
                gen.gen_expr(let->expr);
                gen.m_expect_signed = prev_expect_signed;
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

                ValueType rhs_type = gen.infer_expr_type(assign->expr);
                if (it->type != ValueType::UNKNOWN && rhs_type != ValueType::UNKNOWN && rhs_type != it->type) {
                    std::cerr << "[Type Error] Cannot assign to '" << assign->ident.value.value()
                               << "': type mismatch (variable's type cannot change after declaration)" << std::endl;
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
            void operator()(const NodeStmtWhile* while_) const {
                gen.m_output << "    ;; while\n";
                std::string start_label = gen.create_label();
                std::string end_label = gen.create_label();
                gen.m_output << start_label << ":\n";
                gen.gen_expr(while_->expr);
                gen.pop("rax");
                gen.m_output << "    test rax, rax\n";
                gen.m_output << "    jz " << end_label << "\n";
                gen.gen_scope(while_->scope);
                gen.m_output << "    jmp " << start_label << "\n";
                gen.m_output << end_label << ":\n";
                gen.m_output << "    ;; /while\n";
            }
            void operator()(const NodeStmtFor* for_) const {
                gen.m_output << "    ;; for\n";
                gen.gen_stmt(for_->init);
                std::string start_label = gen.create_label();
                std::string end_label = gen.create_label();
                gen.m_output << start_label << ":\n";
                gen.gen_expr(for_->cond);
                gen.pop("rax");
                gen.m_output << "    test rax, rax\n";
                gen.m_output << "    jz " << end_label << "\n";
                gen.gen_scope(for_->scope);
                gen.gen_stmt(for_->update);
                gen.m_output << "    jmp " << start_label << "\n";
                gen.m_output << end_label << ":\n";
                gen.m_output << "    ;; /for\n";
            }
            void operator()(const NodeStmtDerefAssign* da) const {
                FullType inner = gen.infer_expr_full_type(da->ptr_expr);
                if (inner.ptr_level < 1) {
                    std::cerr << "[Type Error] Cannot dereference a non-pointer value with '*' in assignment" << std::endl;
                    exit(EXIT_FAILURE);
                }
                gen.gen_expr(da->value_expr); // значение
                gen.gen_expr(da->ptr_expr);   // адрес
                gen.pop("rax"); // адрес
                gen.pop("rbx"); // значение
                int bytes = (inner.ptr_level > 1) ? 8 : type_size_bytes(inner.base_type);
                switch (bytes) {
                    case 1: gen.m_output << "    mov [rax], bl\n"; break;
                    case 2: gen.m_output << "    mov [rax], bx\n"; break;
                    case 4: gen.m_output << "    mov [rax], ebx\n"; break;
                    default: gen.m_output << "    mov [rax], rbx\n"; break; // 8/16 байт -> пишем 8 байт
                }
            }
        };
        std::visit(Visitor{*this}, stmt->var);
    }

    void collect_from_prog(const NodeProg& prog) {
        for (const auto* func : prog.funcs) {
            collect_from_scope(func->body);
        }
    }

    void collect_from_scope(const NodeScope* scope) {
        for (const auto* stmt : scope->stmts) {
            collect_from_stmt(stmt);
        }
    }

    void collect_from_stmt(const NodeStmt* stmt) {
        struct Visitor {
            Generator& gen;
            void operator()(const NodeStmtWin* w) { gen.collect_from_expr(w->expr); }
            void operator()(const NodeStmtLet* l) { gen.collect_from_expr(l->expr); }
            void operator()(const NodeStmtAssign* a) { gen.collect_from_expr(a->expr); }
            void operator()(const NodeStmtCut*) {}
            void operator()(const NodeStmtRadio* r) { gen.collect_from_expr(r->expr); }
            void operator()(const NodeScope* s) { gen.collect_from_scope(s); }
            void operator()(const NodeStmtIf* i) {
                gen.collect_from_expr(i->expr);
                gen.collect_from_scope(i->scope);
                if (i->pred.has_value()) gen.collect_from_pred(i->pred.value());
            }
            void operator()(const NodeStmtWhile* w) {
                gen.collect_from_expr(w->expr);
                gen.collect_from_scope(w->scope);
            }
            void operator()(const NodeStmtFor* f) {
                gen.collect_from_stmt(f->init);
                gen.collect_from_expr(f->cond);
                gen.collect_from_stmt(f->update);
                gen.collect_from_scope(f->scope);
            }
            void operator()(const NodeStmtDerefAssign* da) {
                gen.collect_from_expr(da->ptr_expr);
                gen.collect_from_expr(da->value_expr);
            }
        };
        std::visit(Visitor{*this}, stmt->var);
    }

    void collect_from_pred(const NodeIfPred* pred) {
        struct Visitor {
            Generator& gen;
            void operator()(const NodeIfPredElif* elif) {
                gen.collect_from_expr(elif->expr);
                gen.collect_from_scope(elif->scope);
                if (elif->pred.has_value()) gen.collect_from_pred(elif->pred.value());
            }
            void operator()(const NodeIfPredElse* else_) {
                gen.collect_from_scope(else_->scope);
            }
        };
        std::visit(Visitor{*this}, pred->var);
    }

    void collect_from_expr(const NodeExpr* expr) {
        struct Visitor {
            Generator& gen;
            void operator()(const NodeTerm* t) { gen.collect_from_term(t); }
            void operator()(const NodeBinExpr* b) { gen.collect_from_bin(b); }
            void operator()(const NodeUnaryExprNot* n) { gen.collect_from_expr(n->expr); }
        };
        std::visit(Visitor{*this}, expr->var);
    }

    void collect_from_bin(const NodeBinExpr* bin) {
        struct Visitor {
            Generator& gen;
            void operator()(const NodeBinExprAdd* e) { gen.collect_from_expr(e->lhs); gen.collect_from_expr(e->rhs); }
            void operator()(const NodeBinExprMulti* e) { gen.collect_from_expr(e->lhs); gen.collect_from_expr(e->rhs); }
            void operator()(const NodeBinExprSub* e) { gen.collect_from_expr(e->lhs); gen.collect_from_expr(e->rhs); }
            void operator()(const NodeBinExprDiv* e) { gen.collect_from_expr(e->lhs); gen.collect_from_expr(e->rhs); }
            void operator()(const NodeBinExprLess* e) { gen.collect_from_expr(e->lhs); gen.collect_from_expr(e->rhs); }
            void operator()(const NodeBinExprGreater* e) { gen.collect_from_expr(e->lhs); gen.collect_from_expr(e->rhs); }
            void operator()(const NodeBinExprEqual* e) { gen.collect_from_expr(e->lhs); gen.collect_from_expr(e->rhs); }
            void operator()(const NodeBinExprNotEqual* e) { gen.collect_from_expr(e->lhs); gen.collect_from_expr(e->rhs); }
            void operator()(const NodeBinExprLessEq* e) { gen.collect_from_expr(e->lhs); gen.collect_from_expr(e->rhs); }
            void operator()(const NodeBinExprGreaterEq* e) { gen.collect_from_expr(e->lhs); gen.collect_from_expr(e->rhs); }
            void operator()(const NodeBinExprAnd* e) { gen.collect_from_expr(e->lhs); gen.collect_from_expr(e->rhs); }
            void operator()(const NodeBinExprOr* e) { gen.collect_from_expr(e->lhs); gen.collect_from_expr(e->rhs); }
            void operator()(const NodeBinExprNor* e) { gen.collect_from_expr(e->lhs); gen.collect_from_expr(e->rhs); }
        };
        std::visit(Visitor{*this}, bin->var);
    }

    void collect_from_term(const NodeTerm* term) {
        struct Visitor {
            Generator& gen;
            void operator()(const NodeTermStrLit* s) {
                if (std::ranges::find(gen.m_string_literals, s->value) == gen.m_string_literals.end()) {
                    gen.m_string_literals.push_back(s->value);
                }
            }
            void operator()(const NodeTermToString* c) {
                if (!gen.m_conv_buffer_index.contains(c)) {
                    gen.m_conv_buffer_index[c] = gen.m_conv_buffer_count++;
                }
                gen.collect_from_expr(c->expr);
            }
            void operator()(const NodeTermParen* p) { gen.collect_from_expr(p->expr); }
            void operator()(const NodeTermRank* r) { gen.collect_from_expr(r->expr); }
            void operator()(const NodeTermSizeof* s) { gen.collect_from_expr(s->expr); }
            void operator()(const NodeCall* call) {
                for (auto* a : call->args) gen.collect_from_expr(a);
            }
            void operator()(const NodeTermIntLit*) {}
            void operator()(const NodeTermShortLit*) {}
            void operator()(const NodeTermLongLit*) {}
            void operator()(const NodeTermLongLongLit*) {}
            void operator()(const NodeTermFloatLit*) {}
            void operator()(const NodeTermDoubleLit*) {}
            void operator()(const NodeTermBoolLit*) {}
            void operator()(const NodeTermCharLit*) {}
            void operator()(const NodeTermIdent*) {}
            void operator()(const NodeTermType*) {}
            void operator()(const NodeTermCast* c) {
                if (c->expr != nullptr) gen.collect_from_expr(c->expr);
            }
            void operator()(const NodeTermMud*) {
                std::cerr << "[Type Error] `MUD` used without an explicit type cast "
                             "(e.g. `(string) MUD`); a value/variable cannot be untyped" << std::endl;
                exit(EXIT_FAILURE);
            }
            void operator()(const NodeTermAddrOf*) {}
            void operator()(const NodeTermDeref* d) { gen.collect_from_expr(d->expr); }
        };
        std::visit(Visitor{*this}, term->var);
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
        ValueType type = ValueType::UNKNOWN;
        bool is_signed = false;
        int ptr_level = 0;
        ValueType base_type = ValueType::UNKNOWN;
    };

    static constexpr size_t HEAP_SIZE_BYTES = 16 * 1024 * 1024;

    const NodeProg& m_prog;
    std::stringstream m_output;
    size_t m_stack_size = 0;
    std::vector<Var> m_vars;
    std::vector<size_t> m_scopes;
    int m_label_count = 0;
    std::string m_current_epilog;
    std::unordered_map<std::string, int> m_param_offsets;
    std::vector<std::string> m_string_literals;
    bool m_expect_signed = false;
    std::unordered_map<const NodeTermToString*, int> m_conv_buffer_index;
    int m_conv_buffer_count = 0;
};