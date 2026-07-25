#pragma once

#include <string>
#include <vector>
#include <cassert>
#include <optional>

enum class TokenType {
    WIN,
    INT_LIT,
    STRING_LIT,
    FLOAT_LIT,
    DOUBLE_LIT,
    CHAR_LIT,
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
    TRUE,
    FALSE,
    PIN,
    COMMA,
    RADIO,
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
                } else {
                    tokens.push_back({ TokenType::IDENT, line_count, buf });
                }
                buf.clear();
            } else if (std::isdigit(peek().value()) || peek().value() == '.') {
                bool has_dot = false;
                buf.push_back(consume());
                while (peek().has_value() && (std::isdigit(peek().value()) || peek().value() == '.')) {
                    if (peek().value() == '.') {
                        if (has_dot) break;
                        has_dot = true;
                    }
                    buf.push_back(consume());
                }
                TokenType type = TokenType::INT_LIT;
                if (peek().has_value() && (peek().value() == 'f' || peek().value() == 'd')) {
                    char suffix = consume();
                    if (suffix == 'f') type = TokenType::FLOAT_LIT;
                    else if (suffix == 'd') type = TokenType::DOUBLE_LIT;
                } else if (has_dot) {
                    std::cerr << "Floating literal must have suffix 'f' or 'd' at line " << line_count << std::endl;
                    exit(EXIT_FAILURE);
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
            else if (peek().value() == '{') { consume(); tokens.push_back({ TokenType::OPEN_CURLY, line_count }); }
            else if (peek().value() == '}') { consume(); tokens.push_back({ TokenType::CLOSE_CURLY, line_count }); }
            else if (peek().value() == ',') { consume(); tokens.push_back({ TokenType::COMMA, line_count }); }
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