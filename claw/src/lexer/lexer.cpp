#include "lexer/lexer.h"

#include <cctype>
#include <unordered_map>

namespace claw::frontend {

Lexer::Lexer(std::string_view source) : source(source) {}

char Lexer::peek() const {
    if (isAtEnd()) return '\0';
    return source[pos];
}

char Lexer::advance() {
    char c = source[pos++];
    if (c == '\n') {
        ++line;
        column = 1;
    } else {
        ++column;
    }
    return c;
}

bool Lexer::isAtEnd() const {
    return pos >= source.length();
}

void Lexer::skipWhitespace() {
    while (!isAtEnd()) {
        const unsigned char c = static_cast<unsigned char>(peek());
        if (std::isspace(c)) {
            advance();
        } else if (peek() == '/' && pos + 1 < source.length() && source[pos + 1] == '/') {
            while (!isAtEnd() && peek() != '\n') {
                advance();
            }
        } else {
            break;
        }
    }
}

Token Lexer::makeToken(
    TokenKind kind,
    std::string text,
    size_t startLine,
    size_t startColumn,
    size_t tokenLength) const {
    return Token{kind, std::move(text), startLine, startColumn, tokenLength};
}

Token Lexer::readIdentifierOrKeyword() {
    const size_t startLine = line;
    const size_t startColumn = column;
    const size_t startPos = pos;

    std::string text;
    while (!isAtEnd() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
        text += advance();
    }

    static const std::unordered_map<std::string, TokenKind> keywords = {
        {"fn", TokenKind::KwFn}, {"hold", TokenKind::KwHold}, {"slot", TokenKind::KwSlot},
        {"give", TokenKind::KwGive}, {"when", TokenKind::KwWhen}, {"otherwise", TokenKind::KwOtherwise},
        {"loop", TokenKind::KwLoop}, {"scan", TokenKind::KwScan}, {"stop", TokenKind::KwStop},
        {"skip", TokenKind::KwSkip}, {"shape", TokenKind::KwShape}, {"choice", TokenKind::KwChoice},
        {"realm", TokenKind::KwRealm}, {"import", TokenKind::KwImport}, {"share", TokenKind::KwShare},
        {"super", TokenKind::KwSuper},
        {"pick", TokenKind::KwPick}, {"lift", TokenKind::KwLift}, {"raw", TokenKind::KwRaw},
        {"as", TokenKind::KwAs}, {"over", TokenKind::KwOver}, {"of", TokenKind::KwOf},
        {"fail", TokenKind::KwFail}, {"look", TokenKind::KwLook}, {"edit", TokenKind::KwEdit}
    };

    const auto it = keywords.find(text);
    return makeToken(
        it != keywords.end() ? it->second : TokenKind::Identifier,
        text,
        startLine,
        startColumn,
        pos - startPos);
}

Token Lexer::readNumber() {
    const size_t startLine = line;
    const size_t startColumn = column;
    const size_t startPos = pos;

    std::string text;
    while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
        text += advance();
    }

    bool isFloat = false;
    if (!isAtEnd() && peek() == '.' && pos + 1 < source.length() &&
        std::isdigit(static_cast<unsigned char>(source[pos + 1]))) {
        isFloat = true;
        text += advance();
        while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
            text += advance();
        }
    }

    const auto hasExponent = [&]() {
        if (isAtEnd() || (peek() != 'e' && peek() != 'E')) {
            return false;
        }

        size_t nextPos = pos + 1;
        if (nextPos < source.length() && (source[nextPos] == '+' || source[nextPos] == '-')) {
            ++nextPos;
        }

        return nextPos < source.length() && std::isdigit(static_cast<unsigned char>(source[nextPos]));
    };

    if (hasExponent()) {
        isFloat = true;
        text += advance();
        if (!isAtEnd() && (peek() == '+' || peek() == '-')) {
            text += advance();
        }
        while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
            text += advance();
        }
    }

    if (!isAtEnd() && peek() == '_' && pos + 1 < source.length() &&
        std::isalpha(static_cast<unsigned char>(source[pos + 1]))) {
        text += advance();
        while (!isAtEnd() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
            text += advance();
        }
    }

    return makeToken(isFloat ? TokenKind::FloatLiteral : TokenKind::IntLiteral, text, startLine, startColumn, pos - startPos);
}

Token Lexer::readString() {
    const size_t startLine = line;
    const size_t startColumn = column;
    const size_t startPos = pos;

    advance();
    std::string text;
    while (!isAtEnd() && peek() != '"') {
        text += advance();
    }
    if (!isAtEnd()) {
        advance();
    }
    return makeToken(TokenKind::StringLiteral, text, startLine, startColumn, pos - startPos);
}

Token Lexer::readChar() {
    const size_t startLine = line;
    const size_t startColumn = column;
    const size_t startPos = pos;

    advance();
    std::string text;
    if (!isAtEnd() && peek() != '\'') {
        text += advance();
    }
    if (!isAtEnd()) {
        advance();
    }
    return makeToken(TokenKind::CharLiteral, text, startLine, startColumn, pos - startPos);
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (!isAtEnd()) {
        skipWhitespace();
        if (isAtEnd()) {
            break;
        }

        const size_t startLine = line;
        const size_t startColumn = column;
        const size_t startPos = pos;
        const char c = peek();

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            tokens.push_back(readIdentifierOrKeyword());
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            tokens.push_back(readNumber());
            continue;
        }
        if (c == '"') {
            tokens.push_back(readString());
            continue;
        }
        if (c == '\'') {
            tokens.push_back(readChar());
            continue;
        }

        advance();
        switch (c) {
            case '[':
                tokens.push_back(makeToken(TokenKind::LBracket, "[", startLine, startColumn, 1));
                break;
            case ']':
                tokens.push_back(makeToken(TokenKind::RBracket, "]", startLine, startColumn, 1));
                break;
            case '{':
                tokens.push_back(makeToken(TokenKind::LBrace, "{", startLine, startColumn, 1));
                break;
            case '}':
                tokens.push_back(makeToken(TokenKind::RBrace, "}", startLine, startColumn, 1));
                break;
            case '(':
                tokens.push_back(makeToken(TokenKind::LParen, "(", startLine, startColumn, 1));
                break;
            case ')':
                tokens.push_back(makeToken(TokenKind::RParen, ")", startLine, startColumn, 1));
                break;
            case ':':
                tokens.push_back(makeToken(TokenKind::Colon, ":", startLine, startColumn, 1));
                break;
            case '=':
                if (!isAtEnd() && peek() == '=') {
                    advance();
                    tokens.push_back(makeToken(TokenKind::EqEq, "==", startLine, startColumn, pos - startPos));
                } else {
                    tokens.push_back(makeToken(TokenKind::Equal, "=", startLine, startColumn, 1));
                }
                break;
            case '-':
                if (!isAtEnd() && peek() == '>') {
                    advance();
                    tokens.push_back(makeToken(TokenKind::Arrow, "->", startLine, startColumn, pos - startPos));
                } else {
                    tokens.push_back(makeToken(TokenKind::Minus, "-", startLine, startColumn, 1));
                }
                break;
            case ',':
                tokens.push_back(makeToken(TokenKind::Comma, ",", startLine, startColumn, 1));
                break;
            case '.':
                tokens.push_back(makeToken(TokenKind::Dot, ".", startLine, startColumn, 1));
                break;
            case '!':
                if (!isAtEnd() && peek() == '=') {
                    advance();
                    tokens.push_back(makeToken(TokenKind::NotEq, "!=", startLine, startColumn, pos - startPos));
                } else {
                    tokens.push_back(makeToken(TokenKind::Bang, "!", startLine, startColumn, 1));
                }
                break;
            case '<':
                if (!isAtEnd() && peek() == '=') {
                    advance();
                    tokens.push_back(makeToken(TokenKind::LessEq, "<=", startLine, startColumn, pos - startPos));
                } else {
                    tokens.push_back(makeToken(TokenKind::Less, "<", startLine, startColumn, 1));
                }
                break;
            case '>':
                if (!isAtEnd() && peek() == '=') {
                    advance();
                    tokens.push_back(makeToken(TokenKind::GreaterEq, ">=", startLine, startColumn, pos - startPos));
                } else {
                    tokens.push_back(makeToken(TokenKind::Greater, ">", startLine, startColumn, 1));
                }
                break;
            case '+':
                tokens.push_back(makeToken(TokenKind::Plus, "+", startLine, startColumn, 1));
                break;
            case '*':
                tokens.push_back(makeToken(TokenKind::Star, "*", startLine, startColumn, 1));
                break;
            case '/':
                tokens.push_back(makeToken(TokenKind::Slash, "/", startLine, startColumn, 1));
                break;
            default:
                tokens.push_back(makeToken(TokenKind::Error, std::string(1, c), startLine, startColumn, 1));
                break;
        }
    }

    tokens.push_back(makeToken(TokenKind::Eof, "", line, column, 1));
    return tokens;
}

} // namespace claw::frontend