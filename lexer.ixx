// lexer.ixx -- Tokenizer for the .winui language
// Framework-agnostic tokenizer. Supports:
// #version directive, binding refs ($name), @ attributes, ... / ..< ranges,
// full operator set for Pratt expression parsing, // and /* */ comments,
// ternary (?), null-coalescing (??), compound-assignment (*=, /=),
// keywords: let, while, switch, case, default,
// attributes: @computed, @environment.
// Copyright (c) 2026 Momin Shaikh. All rights reserved.

module;
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cctype>
#include <utility>
#include <format>
#include <optional>
export module winui.lexer;

import winui.diagnostics;

export namespace winui::lex {

// Token Kind

enum class TokenKind : std::uint8_t {
    // Literals
    StringLiteral,       // "hello"
    NumberLiteral,       // 42, 3.14
    CssColor,            // hsl(210, 100%, 50%), oklch(0.7, 0.15, 180), color(display-p3, ...)
    Identifier,          // myVar, Column

    // Keywords
    KwImport,   KwFrom,    KwExport,  KwView,
    KwProperty, KwCallback, KwVar,   KwBody,
    KwIf,       KwElse,   KwForEach, KwIn,
    KwTrue,     KwFalse,  KwSelf,    KwRoot,
    KwReturn,
    KwLet,      // let x = expr  (immutable binding, alias for var)
    KwWhile,    // while condition { ... }
    KwSwitch,   // switch expr { case x: ... }
    KwCase,     // case pattern:
    KwDefault,  // default:
    KwIs,       // is  (type test: sender is Button)
    KwAs,       // as  (type cast: sender as Button)

    // Type keywords
    KwString, KwInt, KwBool, KwFloat, KwDouble,

    // Attribute keywords
    // @state  @binding  @published  @computed  @environment
    KwState, KwBinding, KwPublished, KwComputed, KwEnvironment,

    // Delimiters
    LBrace, RBrace, LParen, RParen, LBracket, RBracket,
    Semicolon, Colon, Comma, Dot,
    DotDotDot,         // ...  (inclusive range)
    DotDotLess,        // ..<  (exclusive range)
    At,                // @
    Dollar,            // $name (binding ref)
    Arrow,             // =>
    Hash,              // #  (version directive)

    // Operators
    Plus, Minus, Star, Slash,
    Equal, EqualEqual, BangEqual,
    Less, LessEqual, Greater, GreaterEqual,
    Bang, AmpAmp, PipePipe,
    PlusEqual, MinusEqual,
    StarEqual,         // *=
    SlashEqual,        // /=
    Question,          // ?   (ternary condition)
    QuestionQuestion,  // ??  (null-coalescing / default-value)

    // Special
    EndOfFile, Error,
};

// Token name (for diagnostics)

[[nodiscard]] constexpr auto token_name(TokenKind k) -> std::string_view {
    switch (k) {
        case TokenKind::StringLiteral:      return "string literal";
        case TokenKind::NumberLiteral:      return "number literal";
        case TokenKind::CssColor:           return "CSS color";
        case TokenKind::Identifier:         return "identifier";
        case TokenKind::KwImport:           return "'import'";
        case TokenKind::KwFrom:             return "'from'";
        case TokenKind::KwExport:           return "'export'";
        case TokenKind::KwView:             return "'view'";
        case TokenKind::KwProperty:         return "'property'";
        case TokenKind::KwCallback:         return "'callback'";
        case TokenKind::KwVar:              return "'var'";
        case TokenKind::KwBody:             return "'body'";
        case TokenKind::KwIf:               return "'if'";
        case TokenKind::KwElse:             return "'else'";
        case TokenKind::KwForEach:          return "'ForEach'";
        case TokenKind::KwIn:               return "'in'";
        case TokenKind::KwTrue:             return "'true'";
        case TokenKind::KwFalse:            return "'false'";
        case TokenKind::KwSelf:             return "'self'";
        case TokenKind::KwRoot:             return "'root'";
        case TokenKind::KwReturn:           return "'return'";
        case TokenKind::KwLet:              return "'let'";
        case TokenKind::KwWhile:            return "'while'";
        case TokenKind::KwSwitch:           return "'switch'";
        case TokenKind::KwCase:             return "'case'";
        case TokenKind::KwDefault:          return "'default'";
        case TokenKind::KwString:           return "'string'";
        case TokenKind::KwInt:              return "'int'";
        case TokenKind::KwBool:             return "'bool'";
        case TokenKind::KwFloat:            return "'float'";
        case TokenKind::KwDouble:           return "'double'";
        case TokenKind::KwState:            return "'@state'";
        case TokenKind::KwBinding:          return "'@binding'";
        case TokenKind::KwPublished:        return "'@published'";
        case TokenKind::KwComputed:         return "'@computed'";
        case TokenKind::KwEnvironment:      return "'@environment'";
        case TokenKind::LBrace:             return "'{'";
        case TokenKind::RBrace:             return "'}'";
        case TokenKind::LParen:             return "'('";
        case TokenKind::RParen:             return "')'";
        case TokenKind::LBracket:           return "'['";
        case TokenKind::RBracket:           return "']'";
        case TokenKind::Semicolon:          return "';'";
        case TokenKind::Colon:              return "':'";
        case TokenKind::Comma:              return "','";
        case TokenKind::Dot:                return "'.'";
        case TokenKind::DotDotDot:          return "'...'";
        case TokenKind::DotDotLess:         return "'..<'";
        case TokenKind::At:                 return "'@'";
        case TokenKind::Dollar:             return "'$'";
        case TokenKind::Arrow:              return "'=>'";
        case TokenKind::Hash:               return "'#'";
        case TokenKind::Plus:               return "'+'";
        case TokenKind::Minus:              return "'-'";
        case TokenKind::Star:               return "'*'";
        case TokenKind::Slash:              return "'/'";
        case TokenKind::Equal:              return "'='";
        case TokenKind::EqualEqual:         return "'=='";
        case TokenKind::BangEqual:          return "'!='";
        case TokenKind::Less:               return "'<'";
        case TokenKind::LessEqual:          return "'<='";
        case TokenKind::Greater:            return "'>'";
        case TokenKind::GreaterEqual:       return "'>='";
        case TokenKind::Bang:               return "'!'";
        case TokenKind::AmpAmp:             return "'&&'";
        case TokenKind::PipePipe:           return "'||'";
        case TokenKind::PlusEqual:          return "'+='";
        case TokenKind::MinusEqual:         return "'-='";
        case TokenKind::StarEqual:          return "'*='";
        case TokenKind::SlashEqual:         return "'/='";
        case TokenKind::Question:           return "'?'";
        case TokenKind::QuestionQuestion:   return "'??'";
        case TokenKind::EndOfFile:          return "end of file";
        case TokenKind::Error:              return "error";
    }
    std::unreachable();
}

// Token

struct Token {
    TokenKind   kind;
    std::string lexeme;
    std::size_t line   = 1;
    std::size_t column = 1;

    [[nodiscard]] auto location(std::string_view file) const
        -> diag::SourceLocation {
        return { .file = std::string(file), .line = line, .column = column };
    }
};

// Keyword / Attribute tables

inline auto keywords()
    -> std::unordered_map<std::string_view, TokenKind> const& {
    static const std::unordered_map<std::string_view, TokenKind> t = {
        {"import",   TokenKind::KwImport},   {"from",     TokenKind::KwFrom},
        {"export",   TokenKind::KwExport},   {"view",     TokenKind::KwView},
        {"property", TokenKind::KwProperty}, {"callback", TokenKind::KwCallback},
        {"var",      TokenKind::KwVar},      {"body",     TokenKind::KwBody},
        {"if",       TokenKind::KwIf},       {"else",     TokenKind::KwElse},
        {"ForEach",  TokenKind::KwForEach},  {"in",       TokenKind::KwIn},
        {"true",     TokenKind::KwTrue},     {"false",    TokenKind::KwFalse},
        {"self",     TokenKind::KwSelf},     {"root",     TokenKind::KwRoot},
        {"return",   TokenKind::KwReturn},
        {"let",      TokenKind::KwLet},
        {"while",    TokenKind::KwWhile},
        {"switch",   TokenKind::KwSwitch},
        {"case",     TokenKind::KwCase},
        {"default",  TokenKind::KwDefault},
        {"string",   TokenKind::KwString},   {"int",      TokenKind::KwInt},
        {"bool",     TokenKind::KwBool},     {"float",    TokenKind::KwFloat},
        {"double",   TokenKind::KwDouble},
        {"is",       TokenKind::KwIs},       {"as",      TokenKind::KwAs},
    };
    return t;
}

inline auto attributes()
    -> std::unordered_map<std::string_view, TokenKind> const& {
    static const std::unordered_map<std::string_view, TokenKind> t = {
        {"state",       TokenKind::KwState},
        {"binding",     TokenKind::KwBinding},
        {"published",   TokenKind::KwPublished},
        {"computed",    TokenKind::KwComputed},
        {"environment", TokenKind::KwEnvironment},
    };
    return t;
}

// Lexer

class Lexer {
public:
    explicit Lexer(std::string source, std::string file = "<input>")
        : m_src(std::move(source)), m_file(std::move(file)) {}

    [[nodiscard]] auto tokenize() -> std::vector<Token> {
        std::vector<Token> toks;
        toks.reserve(m_src.size() / 4);
        while (!eof()) {
            skip_trivia();
            if (eof()) break;
            toks.push_back(scan());
        }
        toks.push_back(tok(TokenKind::EndOfFile, "", m_ln, m_co));
        return toks;
    }

    [[nodiscard]] auto take_diagnostics() -> diag::DiagnosticBag {
        return std::move(m_diag);
    }

private:
    std::string         m_src;
    std::string         m_file;
    std::size_t         m_pos = 0;
    std::size_t         m_ln  = 1;
    std::size_t         m_co  = 1;
    diag::DiagnosticBag m_diag;

    [[nodiscard]] auto eof()  const noexcept -> bool { return m_pos >= m_src.size(); }
    [[nodiscard]] auto ch()   const noexcept -> char { return eof() ? '\0' : m_src[m_pos]; }
    [[nodiscard]] auto ch1()  const noexcept -> char { return m_pos+1 < m_src.size() ? m_src[m_pos+1] : '\0'; }

    auto eat() noexcept -> char {
        char c = m_src[m_pos++];
        if (c == '\n') { m_ln++; m_co = 1; } else { m_co++; }
        return c;
    }
    auto eat_if(char e) noexcept -> bool {
        if (eof() || m_src[m_pos] != e) return false;
        eat(); return true;
    }

    static auto is_a(char c) noexcept -> bool { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'; }
    static auto is_d(char c) noexcept -> bool { return c>='0'&&c<='9'; }
    static auto is_w(char c) noexcept -> bool { return is_a(c)||is_d(c); }

    auto skip_trivia() -> void {
        while (!eof()) {
            char c = ch();
            if (c==' '||c=='\t'||c=='\r'||c=='\n') { eat(); continue; }
            if (c=='/'&&ch1()=='/') { while (!eof()&&ch()!='\n') eat(); continue; }
            if (c=='/'&&ch1()=='*') {
                eat(); eat();
                while (!eof()) { if (ch()=='*'&&ch1()=='/') { eat(); eat(); break; } eat(); }
                continue;
            }
            break;
        }
    }

    auto scan() -> Token {
        auto sl = m_ln, sc = m_co;
        char c = eat();

        switch (c) {
            case '{': return tok(TokenKind::LBrace,    "{",  sl, sc);
            case '}': return tok(TokenKind::RBrace,    "}",  sl, sc);
            case '(': return tok(TokenKind::LParen,    "(",  sl, sc);
            case ')': return tok(TokenKind::RParen,    ")",  sl, sc);
            case '[': return tok(TokenKind::LBracket,  "[",  sl, sc);
            case ']': return tok(TokenKind::RBracket,  "]",  sl, sc);
            case ';': return tok(TokenKind::Semicolon, ";",  sl, sc);
            case ':': return tok(TokenKind::Colon,     ":",  sl, sc);
            case ',': return tok(TokenKind::Comma,     ",",  sl, sc);
            case '#': return tok(TokenKind::Hash,      "#",  sl, sc);
            default: break;
        }

        // Dot variants: .  ...  ..<  .75 (leading-dot decimal)
        if (c == '.') {
            if (ch()=='.'&&ch1()=='.') { eat(); eat(); return tok(TokenKind::DotDotDot, "...", sl, sc); }
            if (ch()=='.'&&ch1()=='<') { eat(); eat(); return tok(TokenKind::DotDotLess,"..<", sl, sc); }
            if (is_d(ch())) return scan_number(c, sl, sc);
            return tok(TokenKind::Dot, ".", sl, sc);
        }

        // = == =>
        if (c == '=') {
            if (eat_if('=')) return tok(TokenKind::EqualEqual, "==", sl, sc);
            if (eat_if('>')) return tok(TokenKind::Arrow,      "=>", sl, sc);
            return tok(TokenKind::Equal, "=", sl, sc);
        }

        // Operators with optional compound-assignment suffix
        if (c == '!') { return eat_if('=') ? tok(TokenKind::BangEqual,    "!=",sl,sc) : tok(TokenKind::Bang,    "!",sl,sc); }
        if (c == '<') { return eat_if('=') ? tok(TokenKind::LessEqual,    "<=",sl,sc) : tok(TokenKind::Less,    "<",sl,sc); }
        if (c == '>') { return eat_if('=') ? tok(TokenKind::GreaterEqual, ">=",sl,sc) : tok(TokenKind::Greater, ">",sl,sc); }
        if (c == '+') { return eat_if('=') ? tok(TokenKind::PlusEqual,    "+=",sl,sc) : tok(TokenKind::Plus,    "+",sl,sc); }
        if (c == '-') { return eat_if('=') ? tok(TokenKind::MinusEqual,   "-=",sl,sc) : tok(TokenKind::Minus,   "-",sl,sc); }
        if (c == '*') { return eat_if('=') ? tok(TokenKind::StarEqual,    "*=",sl,sc) : tok(TokenKind::Star,    "*",sl,sc); }
        if (c == '/') { return eat_if('=') ? tok(TokenKind::SlashEqual,   "/=",sl,sc) : tok(TokenKind::Slash,   "/",sl,sc); }

        // Logical operators
        if (c == '&' && eat_if('&')) return tok(TokenKind::AmpAmp,  "&&", sl, sc);
        if (c == '|' && eat_if('|')) return tok(TokenKind::PipePipe, "||", sl, sc);

        // Ternary / null-coalescing
        if (c == '?') {
            return eat_if('?') ? tok(TokenKind::QuestionQuestion, "??", sl, sc)
                               : tok(TokenKind::Question,          "?",  sl, sc);
        }

        if (c == '@') return scan_attr(sl, sc);
        if (c == '$') return scan_dollar(sl, sc);
        if (c == '"') return scan_string(sl, sc);
        if (is_d(c))  return scan_number(c, sl, sc);
        if (is_a(c))  return scan_ident(c, sl, sc);

        m_diag.error({.file=m_file, .line=sl, .column=sc},
            std::format("unexpected character '{}'", c));
        return tok(TokenKind::Error, std::string(1,c), sl, sc);
    }

    auto scan_attr(std::size_t sl, std::size_t sc) -> Token {
        std::string w;
        while (!eof() && is_a(ch())) w += eat();
        auto const& a = attributes();
        if (auto it = a.find(w); it != a.end())
            return tok(it->second, "@" + w, sl, sc);
        m_diag.error({.file=m_file,.line=sl,.column=sc},
            std::format("unknown attribute '@{}'", w));
        return tok(TokenKind::Error, "@" + w, sl, sc);
    }

    auto scan_dollar(std::size_t sl, std::size_t sc) -> Token {
        std::string n;
        while (!eof() && is_w(ch())) n += eat();
        if (n.empty()) {
            m_diag.error({.file=m_file,.line=sl,.column=sc}, "expected identifier after '$'");
            return tok(TokenKind::Error, "$", sl, sc);
        }
        return tok(TokenKind::Dollar, "$" + n, sl, sc);
    }

    auto scan_string(std::size_t sl, std::size_t sc) -> Token {
        std::string v;
        while (!eof() && ch() != '"') {
            if (ch() == '\\') {
                eat();
                if (eof()) break;
                char e = eat();
                if (e == '(') {
                    v += "\\("; // string interpolation marker
                } else if (e == '"') {
                    v += '"';   // \" → " (quoted text round-trips exactly)
                } else if (e == '\\') {
                    if (!eof() && ch() == '"') {
                        eat();
                        v += "\\\"";
                    } else {
                        v += "\\\\";
                    }
                } else if (e == 'u') {
                    // \uXXXX → glyph escape ONLY when exactly 4 hex digits
                    // follow (e.g. "\uE74E"). Otherwise the backslash is a
                    // literal Windows path separator, as in "assets\icon.png"
                    // or "C:\users\me\icon.png".
                    bool hex4 = true;
                    for (int i = 0; i < 4 && hex4; ++i) {
                        char h = (m_pos + i < m_src.size()) ? m_src[m_pos + i] : '\0';
                        hex4 = (h >= '0' && h <= '9') ||
                               (h >= 'a' && h <= 'f') ||
                               (h >= 'A' && h <= 'F');
                    }
                    if (hex4) { v += scan_unicode_escape(sl, sc); }
                    else { v += '\\'; v += 'u'; }
                } else {
                    v += '\\'; // literal backslash — paths use single '\'
                    v += e;
                }
            } else { v += eat(); }
        }
        if (eof()) {
            m_diag.error({.file=m_file,.line=sl,.column=sc}, "unterminated string");
            return tok(TokenKind::Error, v, sl, sc);
        }
        eat(); // closing "
        return tok(TokenKind::StringLiteral, "\"" + v + "\"", sl, sc);
    }

    // \uXXXX → UTF-8 (Segoe MDL2 glyph escapes like \uE74E)
    auto scan_unicode_escape(std::size_t sl, std::size_t sc) -> std::string {
        auto hex_val = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        unsigned cp = 0;
        for (int i = 0; i < 4; ++i) {
            if (eof()) break;
            char h = ch();
            int hv = hex_val(h);
            if (hv < 0) {
                m_diag.error({.file=m_file,.line=sl,.column=sc},
                    std::format("invalid unicode escape '\\u{}...'", std::string(1, h)));
                return "\uFFFD";
            }
            eat();
            cp = (cp << 4) | static_cast<unsigned>(hv);
        }
        // Encode code point as UTF-8
        std::string out;
        if (cp <= 0x7F) {
            out += static_cast<char>(cp);
        } else if (cp <= 0x7FF) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        return out;
    }

    auto scan_number(char first, std::size_t sl, std::size_t sc) -> Token {
        std::string n(1, first);
        while (!eof() && is_d(ch())) n += eat();
        // Decimal -- but NOT if followed by another dot (range: 0...100)
        if (!eof() && ch()=='.' && ch1()!='.' && ch1()!='<' && is_d(ch1())) {
            n += eat();
            while (!eof() && is_d(ch())) n += eat();
        }
        if (n.size() >= 2 && n[0] == '.' && is_d(n[1])) n.insert(n.begin(), '0');
        return tok(TokenKind::NumberLiteral, std::move(n), sl, sc);
    }

    auto scan_ident(char first, std::size_t sl, std::size_t sc) -> Token {
        std::string id(1, first);
        while (!eof() && is_w(ch())) id += eat();
        // CSS color functions: hsl(), hsla(), oklch(), color(display-p3, ...)
        // When followed by '(', consume the entire function call as a CssColor token.
        if (!eof() && ch() == '(' && (id == "hsl" || id == "hsla" || id == "oklch" || id == "color")) {
            std::string css = id + eat(); // consume '('
            int depth = 1;
            while (!eof() && depth > 0) {
                char c = ch();
                if (c == '(') ++depth;
                else if (c == ')') --depth;
                css += eat();
            }
            // Validate it looks like a CSS color (must contain a comma or be display-p3)
            if (css.find(',') != std::string::npos || css.find("display-p3") != std::string::npos)
                return tok(TokenKind::CssColor, std::move(css), sl, sc);
            // Not a CSS color — error: emit as identifier and let parser deal with it
            return tok(TokenKind::Identifier, std::move(id), sl, sc);
        }
        auto const& kw = keywords();
        if (auto it = kw.find(id); it != kw.end())
            return tok(it->second, std::move(id), sl, sc);
        return tok(TokenKind::Identifier, std::move(id), sl, sc);
    }

    static auto tok(TokenKind k, std::string lex, std::size_t l, std::size_t c) -> Token {
        return Token{.kind=k, .lexeme=std::move(lex), .line=l, .column=c};
    }
};

// Free function

[[nodiscard]] inline auto tokenize(std::string src, std::string file = "<input>")
    -> std::pair<std::vector<Token>, diag::DiagnosticBag> {
    Lexer lex(std::move(src), std::move(file));
    auto toks = lex.tokenize();
    return { std::move(toks), lex.take_diagnostics() };
}

} // namespace winui::lex
