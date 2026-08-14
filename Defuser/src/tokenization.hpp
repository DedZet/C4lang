#pragma once

#include <string>
#include <vector>
#include <cassert>
#include <optional>
#include <iostream>

enum class TokenType {
    WIN,
    INT_LIT,
    SEMI,
    open_paren,
    close_paren,
    IDENT,
    LET,            // wire
    EQUAL,
    PLUS,
    STAR,
    MINUS,
    FSLAH,
    OPEN_CURLY,
    CLOSE_CURLY,
    IF,
    ELIF,
    ELSE,
    BOMB,
    CUT,
    SPOTTED,
    FLEE,
    CTERR,
    TERR,
    STRING_LIT,
    FLOAT_LIT,
    DOUBLE_LIT,
    CHAR_LIT,
    TRUE,
    FALSE,
    PIN,
    COMMA,
    RADIO,
    LESS,           // <
    GREATER,        // >
    EQUAL_CMP,      // <>  (==)
    NOT_EQUAL_CMP,  // ><  (!=)
    LESS_EQ,        // <<  (<=)
    GREATER_EQ,     // >>  (>=)
    AND,            // and
    OR,             // or
    NOT,            // not
    NOR,            // nor
    RANK,           // rank()
    TYPE_INT,       // int
    TYPE_BOOL,      // bool
    TYPE_CHAR,      // char
    TYPE_STRING,    // string
    TYPE_FLOAT,     // float
    TYPE_DOUBLE,    // double

    TYPE_SHORT,     // short (2 bytes)
    TYPE_LONG,      // long (8 bytes)
    TYPE_LONGLONG,  // longlong (16 bytes)
    SHORT_LIT,      // 123s
    LONG_LIT,       // 123xl
    LONGLONG_LIT,   // 123xxl
    SIGN,           // sign
    MUD,            // MUD
    FOR,            // for
    WHILE,          // while
    SIZEOF,         // sizeof
    AMPERSAND,      // &
};

inline std::string to_string(const TokenType type) {
    switch (type) {
    case TokenType::WIN:            return "`win`";
    case TokenType::INT_LIT:        return "int literal";
    case TokenType::SEMI:           return "`;`";
    case TokenType::open_paren:     return "`(`";
    case TokenType::close_paren:    return "`)`";
    case TokenType::IDENT:          return "identifier";
    case TokenType::LET:            return "`wire`";
    case TokenType::EQUAL:          return "`=`";
    case TokenType::PLUS:           return "`+`";
    case TokenType::STAR:           return "`*`";
    case TokenType::MINUS:          return "`-`";
    case TokenType::FSLAH:          return "`/`";
    case TokenType::OPEN_CURLY:     return "`{`";
    case TokenType::CLOSE_CURLY:    return "`}`";
    case TokenType::IF:             return "`if`";
    case TokenType::ELIF:           return "`elif`";
    case TokenType::ELSE:           return "`else`";
    case TokenType::BOMB:           return "`bomb`";
    case TokenType::CUT:            return "`cut`";
    case TokenType::SPOTTED:        return "`Spotted`";
    case TokenType::FLEE:           return "`Flee`";
    case TokenType::CTERR:          return "`cterr`";
    case TokenType::TERR:           return "`terr`";
    case TokenType::STRING_LIT:     return "string literal";
    case TokenType::FLOAT_LIT:      return "float literal";
    case TokenType::DOUBLE_LIT:     return "double literal";
    case TokenType::CHAR_LIT:       return "char literal";
    case TokenType::TRUE:           return "`True`";
    case TokenType::FALSE:          return "`False`";
    case TokenType::PIN:            return "`pin`";
    case TokenType::COMMA:          return "`,`";
    case TokenType::RADIO:          return "`radio`";
    case TokenType::LESS:           return "`<`";
    case TokenType::GREATER:        return "`>`";
    case TokenType::EQUAL_CMP:      return "`<>`";
    case TokenType::NOT_EQUAL_CMP:  return "`><`";
    case TokenType::LESS_EQ:        return "`<<`";
    case TokenType::GREATER_EQ:     return "`>>`";
    case TokenType::AND:            return "`and`";
    case TokenType::OR:             return "`or`";
    case TokenType::NOT:            return "`not`";
    case TokenType::NOR:            return "`nor`";
    case TokenType::RANK:           return "`rank`";
    case TokenType::TYPE_INT:       return "`int`";
    case TokenType::TYPE_BOOL:      return "`bool`";
    case TokenType::TYPE_CHAR:      return "`char`";
    case TokenType::TYPE_STRING:    return "`string`";
    case TokenType::TYPE_FLOAT:     return "`float`";
    case TokenType::TYPE_DOUBLE:    return "`double`";
    case TokenType::TYPE_SHORT:     return "`short`";
    case TokenType::TYPE_LONG:      return "`long`";
    case TokenType::TYPE_LONGLONG:  return "`longlong`";
    case TokenType::SHORT_LIT:      return "short literal";
    case TokenType::LONG_LIT:       return "long literal";
    case TokenType::LONGLONG_LIT:   return "long long literal";
    case TokenType::SIGN:           return "`sign`";
    case TokenType::MUD:            return "`MUD`";
    case TokenType::FOR:            return "`for`";
    case TokenType::WHILE:          return "`while`";
    case TokenType::SIZEOF:         return "`sizeof`";
    case TokenType::AMPERSAND:      return "`&`";
    default: assert(false);
    }
    return "";
}

inline std::optional<int> bin_prec(const TokenType type) {
    switch (type) {
    case TokenType::MINUS:
    case TokenType::PLUS:    return 0;
    case TokenType::FSLAH:
    case TokenType::STAR:    return 1;
    default:                 return {};
    }
}

struct Token {
    TokenType type;
    int line;
    std::optional<std::string> value {};
};

class Tokenizer {
public:
    explicit Tokenizer(std::string src) : m_src(std::move(src)) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        std::string buf;
        int line_count = 1;
        while (peek().has_value()) {
            if (std::isalpha(peek().value()) || peek().value() == '_') {
                buf.push_back(consume());
                while (peek().has_value() && (std::isalnum(peek().value()) || peek().value() == '_')) {
                    buf.push_back(consume());
                }
                if (buf == "win") {
                    tokens.push_back({ TokenType::WIN, line_count });
                } else if (buf == "wire") {
                    tokens.push_back({ TokenType::LET, line_count });
                } else if (buf == "pin") {
                    tokens.push_back({ TokenType::PIN, line_count });
                } else if (buf == "if") {
                    tokens.push_back({ TokenType::IF, line_count });
                } else if (buf == "elif") {
                    tokens.push_back({ TokenType::ELIF, line_count });
                } else if (buf == "else") {
                    tokens.push_back({ TokenType::ELSE, line_count });
                } else if (buf == "bomb") {
                    tokens.push_back({ TokenType::BOMB, line_count });
                } else if (buf == "cut") {
                    tokens.push_back({ TokenType::CUT, line_count });
                } else if (buf == "Spotted") {
                    tokens.push_back({ TokenType::SPOTTED, line_count });
                } else if (buf == "Flee") {
                    tokens.push_back({ TokenType::FLEE, line_count });
                } else if (buf == "cterr") {
                    tokens.push_back({ TokenType::CTERR, line_count });
                } else if (buf == "terr") {
                    tokens.push_back({ TokenType::TERR, line_count });
                } else if (buf == "radio") {
                    tokens.push_back({ TokenType::RADIO, line_count });
                } else if (buf == "True") {
                    tokens.push_back({ TokenType::TRUE, line_count });
                } else if (buf == "False") {
                    tokens.push_back({ TokenType::FALSE, line_count });
                } else if (buf == "MUD") {
                    tokens.push_back({ TokenType::MUD, line_count });
                } else if (buf == "and") {
                    tokens.push_back({ TokenType::AND, line_count });
                } else if (buf == "or") {
                    tokens.push_back({ TokenType::OR, line_count });
                } else if (buf == "not") {
                    tokens.push_back({ TokenType::NOT, line_count });
                } else if (buf == "nor") {
                    tokens.push_back({ TokenType::NOR, line_count });
                } else if (buf == "rank") {
                    tokens.push_back({ TokenType::RANK, line_count });
                } else if (buf == "int") {
                    tokens.push_back({ TokenType::TYPE_INT, line_count });
                } else if (buf == "bool") {
                    tokens.push_back({ TokenType::TYPE_BOOL, line_count });
                } else if (buf == "char") {
                    tokens.push_back({ TokenType::TYPE_CHAR, line_count });
                } else if (buf == "string") {
                    tokens.push_back({ TokenType::TYPE_STRING, line_count });
                } else if (buf == "float") {
                    tokens.push_back({ TokenType::TYPE_FLOAT, line_count });
                } else if (buf == "double") {
                    tokens.push_back({ TokenType::TYPE_DOUBLE, line_count });
                } else if (buf == "short") {
                    tokens.push_back({ TokenType::TYPE_SHORT, line_count });
                } else if (buf == "longlong") {
                    tokens.push_back({ TokenType::TYPE_LONGLONG, line_count });
                } else if (buf == "long") {
                    tokens.push_back({ TokenType::TYPE_LONG, line_count });
                } else if (buf == "sign") {
                    tokens.push_back({ TokenType::SIGN, line_count });
                } else if (buf == "for") {
                    tokens.push_back({ TokenType::FOR, line_count });
                } else if (buf == "while") {
                    tokens.push_back({ TokenType::WHILE, line_count });
                } else if (buf == "sizeof") {
                    tokens.push_back({ TokenType::SIZEOF, line_count });
                } else {
                    tokens.push_back({ TokenType::IDENT, line_count, buf });
                }
                buf.clear();
            } else if (std::isdigit(peek().value()) || peek().value() == '.') {

                if (peek().value() == '0' && peek(1).has_value() &&
                    (peek(1).value() == 'x' || peek(1).value() == 'X') &&
                    peek(2).has_value() && std::isxdigit(static_cast<unsigned char>(peek(2).value()))) {
                    buf.push_back(consume()); // '0'
                    buf.push_back(consume()); // 'x' / 'X'
                    while (peek().has_value() && std::isxdigit(static_cast<unsigned char>(peek().value()))) {
                        buf.push_back(consume());
                    }
                    tokens.push_back({ TokenType::INT_LIT, line_count, buf });
                    buf.clear();
                    continue;
                }
                bool has_dot = false;
                buf.push_back(consume());
                while (peek().has_value() && (std::isdigit(peek().value()) || peek().value() == '.')) {
                    if (peek().value() == '.') {
                        if (has_dot) break;
                        has_dot = true;
                    }
                    buf.push_back(consume());
                }

                TokenType type;
                if (peek().has_value() && peek().value() == 'f') {
                    consume();
                    type = TokenType::FLOAT_LIT;
                } else if (peek().has_value() && peek().value() == 'd') {
                    consume();
                    type = TokenType::DOUBLE_LIT;
                } else if (!has_dot && peek().has_value() && peek().value() == 's') {
                    consume();
                    type = TokenType::SHORT_LIT;
                } else if (!has_dot && peek().has_value() && peek().value() == 'x'
                           && peek(1).has_value() && peek(1).value() == 'x'
                           && peek(2).has_value() && peek(2).value() == 'l') {
                    consume(); consume(); consume();
                    type = TokenType::LONGLONG_LIT;
                } else if (!has_dot && peek().has_value() && peek().value() == 'x'
                           && peek(1).has_value() && peek(1).value() == 'l') {
                    consume(); consume();
                    type = TokenType::LONG_LIT;
                } else if (has_dot) {
                    type = TokenType::FLOAT_LIT;
                } else {
                    type = TokenType::INT_LIT;
                }
                tokens.push_back({ type, line_count, buf });
                buf.clear();
            } else if (peek().value() == '"') {
                consume();
                while (peek().has_value() && peek().value() != '"') {
                    buf.push_back(consume());
                }
                if (peek().has_value()) consume();
                tokens.push_back({ TokenType::STRING_LIT, line_count, buf });
                buf.clear();
            } else if (peek().value() == '\'') {
                consume();
                if (!peek().has_value()) {
                    std::cerr << "Unterminated char literal at line " << line_count << std::endl;
                    exit(EXIT_FAILURE);
                }
                char ch = consume();
                if (peek().has_value() && peek().value() == '\'') {
                    consume();
                    tokens.push_back({ TokenType::CHAR_LIT, line_count, std::string(1, ch) });
                } else {
                    std::cerr << "Invalid char literal at line " << line_count << std::endl;
                    exit(EXIT_FAILURE);
                }
                buf.clear();
            } else if (peek().value() == '/' && peek(1).has_value() && peek(1).value() == '/') {
                consume(); consume();
                while (peek().has_value() && peek().value() != '\n') consume();
            } else if (peek().value() == '/' && peek(1).has_value() && peek(1).value() == '*') {
                consume(); consume();
                while (peek().has_value()) {
                    if (peek().value() == '*' && peek(1).has_value() && peek(1).value() == '/') break;
                    consume();
                }
                if (peek().has_value()) consume();
                if (peek().has_value()) consume();
            } else if (peek().value() == '(') { consume(); tokens.push_back({ TokenType::open_paren, line_count }); }
            else if (peek().value() == ')') { consume(); tokens.push_back({ TokenType::close_paren, line_count }); }
            else if (peek().value() == ';') { consume(); tokens.push_back({ TokenType::SEMI, line_count }); }
            else if (peek().value() == '=') { consume(); tokens.push_back({ TokenType::EQUAL, line_count }); }
            else if (peek().value() == '+') { consume(); tokens.push_back({ TokenType::PLUS, line_count }); }
            else if (peek().value() == '*') { consume(); tokens.push_back({ TokenType::STAR, line_count }); }
            else if (peek().value() == '-') { consume(); tokens.push_back({ TokenType::MINUS, line_count }); }
            else if (peek().value() == '/') { consume(); tokens.push_back({ TokenType::FSLAH, line_count }); }
            else if (peek().value() == '<') {
                consume();
                if (peek().has_value() && peek().value() == '<') {
                    consume();
                    tokens.push_back({ TokenType::LESS_EQ, line_count });
                } else if (peek().has_value() && peek().value() == '>') {
                    consume();
                    tokens.push_back({ TokenType::EQUAL_CMP, line_count });
                } else {
                    tokens.push_back({ TokenType::LESS, line_count });
                }
            }
            else if (peek().value() == '>') {
                consume();
                if (peek().has_value() && peek().value() == '>') {
                    consume();
                    tokens.push_back({ TokenType::GREATER_EQ, line_count });
                } else if (peek().has_value() && peek().value() == '<') {
                    consume();
                    tokens.push_back({ TokenType::NOT_EQUAL_CMP, line_count });
                } else {
                    tokens.push_back({ TokenType::GREATER, line_count });
                }
            }
            else if (peek().value() == '{') { consume(); tokens.push_back({ TokenType::OPEN_CURLY, line_count }); }
            else if (peek().value() == '}') { consume(); tokens.push_back({ TokenType::CLOSE_CURLY, line_count }); }
            else if (peek().value() == ',') { consume(); tokens.push_back({ TokenType::COMMA, line_count }); }
            else if (peek().value() == '&') { consume(); tokens.push_back({ TokenType::AMPERSAND, line_count }); }
            else if (peek().value() == '\n') { consume(); line_count++; }
            else if (std::isspace(peek().value())) { consume(); }
            else {
                std::cerr << "Invalid token at line " << line_count << std::endl;
                exit(EXIT_FAILURE);
            }
        }
        m_index = 0;
        return tokens;
    }

private:
    std::optional<char> peek(size_t offset = 0) const {
        if (m_index + offset >= m_src.length()) return {};
        return m_src.at(m_index + offset);
    }
    char consume() { return m_src.at(m_index++); }

    const std::string m_src;
    size_t m_index = 0;
};