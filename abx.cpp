#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <openssl/evp.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unistd.h>
#include <vector>

#include "abx.hpp"

namespace {

// ---------------------------------------------------------------------------
// Wire-protocol constants (BinaryXmlSerializer.java / BinaryXmlPullParser.java)
// ---------------------------------------------------------------------------

// Token (low nibble) and Type (high nibble) of each event byte. Declared as
// plain constexpr uint8_t rather than scoped enums so they can be combined
// with `|` into a full event byte without -Wdeprecated-enum-enum-conversion.
constexpr uint8_t START_DOCUMENT          = 0;
constexpr uint8_t END_DOCUMENT           = 1;
constexpr uint8_t START_TAG              = 2;
constexpr uint8_t END_TAG                = 3;
constexpr uint8_t TEXT                   = 4;
constexpr uint8_t CDSECT                 = 5;
constexpr uint8_t ENTITY_REF             = 6;
constexpr uint8_t IGNORABLE_WHITESPACE   = 7;
constexpr uint8_t PROCESSING_INSTRUCTION = 8;
constexpr uint8_t COMMENT                = 9;
constexpr uint8_t DOCDECL                = 10;
constexpr uint8_t ATTRIBUTE              = 15;

constexpr uint8_t TYPE_NULL             = 0x10;
constexpr uint8_t TYPE_STRING          = 0x20;
constexpr uint8_t TYPE_STRING_INTERNED = 0x30;
constexpr uint8_t TYPE_BYTES_HEX       = 0x40;
constexpr uint8_t TYPE_BYTES_BASE64    = 0x50;
constexpr uint8_t TYPE_INT             = 0x60;
constexpr uint8_t TYPE_INT_HEX         = 0x70;
constexpr uint8_t TYPE_LONG            = 0x80;
constexpr uint8_t TYPE_LONG_HEX        = 0x90;
constexpr uint8_t TYPE_FLOAT           = 0xA0;
constexpr uint8_t TYPE_DOUBLE          = 0xB0;
constexpr uint8_t TYPE_BOOLEAN_TRUE    = 0xC0;
constexpr uint8_t TYPE_BOOLEAN_FALSE   = 0xD0;

constexpr uint16_t INTERN_SENTINEL = 0xFFFF; // == FastDataOutput.MAX_UNSIGNED_SHORT

// ---------------------------------------------------------------------------
// Big-endian primitives (host-independent; std::byteswap is C++23, unavailable)
// ---------------------------------------------------------------------------

void write_be16(std::ostream& os, uint16_t v) {
    os.put(static_cast<char>((v >> 8) & 0xFF));
    os.put(static_cast<char>(v & 0xFF));
}

// ---------------------------------------------------------------------------
// Modified UTF-8 codec (ModifiedUtf8.java). Encodes over UTF-16 code units:
// U+0000 and U+0080..U+07FF -> 2 bytes; U+0800..U+FFFF -> 3 bytes;
// supplementary chars (surrogate pair) -> 6 bytes (CESU-8). For BMP chars
// the on-wire bytes are identical to plain UTF-8.
// ---------------------------------------------------------------------------

static void encode_one_mutf8(std::string& out, uint16_t u) {
    if (u != 0 && u <= 0x7F) {
        out.push_back(static_cast<char>(u));
    } else if (u <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (u >> 6)));
        out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (u >> 12)));
        out.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
    }
}

// UTF-8 (as stored in std::string) -> modified-UTF-8 bytes.
std::string utf8_to_mutf8(std::string_view in, bool& ok) {
    std::string out;
    size_t i = 0;
    while (i < in.size()) {
        uint32_t cp;
        uint8_t b0 = static_cast<uint8_t>(in[i]);
        if (b0 <= 0x7F) {
            cp = b0; i += 1;
        } else if ((b0 & 0xE0) == 0xC0) {
            if (i + 1 >= in.size()) { ok = false; return {}; }
            cp = (uint32_t(b0 & 0x1F) << 6) | (uint8_t(in[i + 1]) & 0x3F);
            i += 2;
        } else if ((b0 & 0xF0) == 0xE0) {
            if (i + 2 >= in.size()) { ok = false; return {}; }
            cp = (uint32_t(b0 & 0x0F) << 12) | (uint32_t(in[i + 1] & 0x3F) << 6)
                 | (uint8_t(in[i + 2]) & 0x3F);
            i += 3;
        } else if ((b0 & 0xF8) == 0xF0) {
            if (i + 3 >= in.size()) { ok = false; return {}; }
            cp = (uint32_t(b0 & 0x07) << 18) | (uint32_t(in[i + 1] & 0x3F) << 12)
                 | (uint32_t(in[i + 2] & 0x3F) << 6) | (uint8_t(in[i + 3]) & 0x3F);
            i += 4;
        } else {
            ok = false; return {};
        }
        if (cp <= 0xFFFF) {
            encode_one_mutf8(out, static_cast<uint16_t>(cp));
        } else {
            const uint32_t v = cp - 0x10000;
            encode_one_mutf8(out, static_cast<uint16_t>(0xD800 + (v >> 10)));
            encode_one_mutf8(out, static_cast<uint16_t>(0xDC00 + (v & 0x3FF)));
        }
    }
    return out;
}

static uint16_t decode_one_mutf8(const char* p, size_t& i, size_t n, bool& ok) {
    uint8_t a = static_cast<uint8_t>(p[i]);
    if (a < 0x80) { i += 1; return a; }
    if ((a & 0xE0) == 0xC0) {
        if (i + 1 >= n) { ok = false; return 0; }
        uint8_t b = static_cast<uint8_t>(p[i + 1]);
        if ((b & 0xC0) != 0x80) { ok = false; return 0; }
        i += 2;
        return uint16_t(((a & 0x1F) << 6) | (b & 0x3F));
    }
    if ((a & 0xF0) == 0xE0) {
        if (i + 2 >= n) { ok = false; return 0; }
        uint8_t b = static_cast<uint8_t>(p[i + 1]);
        uint8_t c = static_cast<uint8_t>(p[i + 2]);
        if ((b & 0xC0) != 0x80 || (c & 0xC0) != 0x80) { ok = false; return 0; }
        i += 3;
        return uint16_t(((a & 0x0F) << 12) | (uint16_t(b & 0x3F) << 6) | (c & 0x3F));
    }
    ok = false;
    return 0;
}

static void append_utf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// modified-UTF-8 bytes -> UTF-8 (std::string). Surrogate pairs combine into
// 4-byte UTF-8; lone surrogates are passed through as 3-byte forms (rare,
// invalid UTF-8, but preserves bytes for round-trip).
std::string mutf8_to_utf8(std::string_view in, bool& ok) {
    std::string out;
    size_t i = 0;
    while (i < in.size()) {
        uint16_t u = decode_one_mutf8(in.data(), i, in.size(), ok);
        if (!ok) return {};
        if (u >= 0xD800 && u <= 0xDBFF) {
            if (i >= in.size()) { ok = false; return {}; }
            uint16_t u2 = decode_one_mutf8(in.data(), i, in.size(), ok);
            if (!ok || u2 < 0xDC00 || u2 > 0xDFFF) { ok = false; return {}; }
            uint32_t cp = 0x10000 + ((uint32_t(u - 0xD800) << 10) | (u2 - 0xDC00));
            append_utf8(out, cp);
        } else {
            append_utf8(out, u);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Output formatting helpers (decoder side) — match Java getValueString()
// ---------------------------------------------------------------------------

// Standard base64 (A-Za-z0-9+/ with '=' padding, no newlines) via libcrypto,
// matching Java Base64.encodeToString(bytes, Base64.NO_WRAP) that
// BinaryXmlPullParser.getValueString() uses for TYPE_BYTES_BASE64.
std::string to_base64(std::string_view bytes) {
    if (bytes.empty()) return {};
    const int len = static_cast<int>(bytes.size());
    const size_t outlen = ((bytes.size() + 2) / 3) * 4;
    std::string out(outlen + 1, '\0'); // +1: EVP_EncodeBlock NUL-terminates
    EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                    reinterpret_cast<const unsigned char*>(bytes.data()),
                    len);
    out.resize(outlen);
    return out;
}

// Lowercase hex via std::format, matching BinaryXmlPullParser.HEX_DIGITS.
std::string to_hex_lower(std::string_view bytes) {
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char c : bytes)
        out += std::format("{:02x}", c);
    return out;
}

// Integer/Long.toString(v, 16): signed-magnitude lowercase hex (no "0x", no
// leading zeros; -255 -> "-ff", INT_MIN -> "-80000000"). The magnitude is
// taken on the unsigned cast (mod 2^N subtraction) to avoid signed-overflow
// UB on the most-negative value; the digit string comes from std::format.
template <typename Signed, typename Unsigned>
std::string signed_mag_hex(Signed v) {
    const Unsigned mag = (v < 0) ? (Unsigned{0} - static_cast<Unsigned>(v))
                                 : static_cast<Unsigned>(v);
    std::string s = std::format("{:x}", mag);
    return v < 0 ? "-" + s : s;
}
std::string int_to_hex(int32_t v)  { return signed_mag_hex<int32_t,  uint32_t>(v); }
std::string long_to_hex(int64_t v) { return signed_mag_hex<int64_t,  uint64_t>(v); }

// Best-effort float/double formatting. Java Float.toString/Double.toString is
// not reproducible from std; we emit shortest to_chars, force a ".0" on whole
// numbers, and uppercase the exponent. NaN/Infinity handled as Java does.
template <typename T>
std::string format_fp(T v) {
    if (std::isnan(v)) return "NaN";
    if (std::isinf(v)) return v < 0 ? "-Infinity" : "Infinity";
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), v);
    if (res.ec != std::errc{}) {
        return std::to_string(v);
    }
    std::string s(buf, res.ptr);
    const bool has_exp = (s.find('e') != std::string::npos) ||
                         (s.find('E') != std::string::npos);
    if (s.find('.') == std::string::npos && !has_exp) s += ".0";
    for (char& c : s) if (c == 'e') c = 'E';
    return s;
}

// ---------------------------------------------------------------------------
// AbxWriter — encodes an XML event stream to binary XML
// ---------------------------------------------------------------------------

class AbxWriter {
    std::ostream& m_out;
    std::unordered_map<std::string, uint16_t> m_intern;
    bool m_ok = true;

    void write_byte(uint8_t b) { m_out.put(static_cast<char>(b)); }
    void write_short(uint16_t v) { write_be16(m_out, v); }
    void write_utf(const std::string& s) {
        bool ok = true;
        std::string mutf = utf8_to_mutf8(s, ok);
        if (!ok) { m_ok = false; return; }
        if (mutf.size() > 0xFFFF) { m_ok = false; return; }
        write_short(static_cast<uint16_t>(mutf.size()));
        m_out.write(mutf.data(), static_cast<std::streamsize>(mutf.size()));
    }
    void write_interned(const std::string& s) {
        auto it = m_intern.find(s);
        if (it != m_intern.end()) { write_short(it->second); return; }
        write_short(INTERN_SENTINEL);
        write_utf(s);
        uint16_t ref = static_cast<uint16_t>(m_intern.size());
        if (ref < INTERN_SENTINEL) m_intern.emplace(s, ref);
    }
    void write_token(uint8_t token, const std::string& s) {
        // Parser never yields null text, so always TYPE_STRING (never TYPE_NULL).
        write_byte(static_cast<uint8_t>(token | TYPE_STRING));
        write_utf(s);
    }

public:
    explicit AbxWriter(std::ostream& os) : m_out(os) {
        static const char magic[4] = { 0x41, 0x42, 0x58, 0x00 };
        m_out.write(magic, 4);
    }
    bool ok() const { return m_ok && m_out.good(); }

    void startDocument() { write_byte(START_DOCUMENT | TYPE_NULL); }
    void endDocument()   { write_byte(END_DOCUMENT | TYPE_NULL); m_out.flush(); }
    void startTag(const std::string& name) {
        write_byte(START_TAG | TYPE_STRING_INTERNED); write_interned(name);
    }
    void endTag(const std::string& name) {
        write_byte(END_TAG | TYPE_STRING_INTERNED); write_interned(name);
    }
    void attribute(const std::string& name, const std::string& val) {
        write_byte(ATTRIBUTE | TYPE_STRING);
        write_interned(name);
        write_utf(val);
    }
    void text(const std::string& s)                 { write_token(TEXT, s); }
    void cdsect(const std::string& s)               { write_token(CDSECT, s); }
    void comment(const std::string& s)             { write_token(COMMENT, s); }
    void pi(const std::string& s)                   { write_token(PROCESSING_INSTRUCTION, s); }
    void docdecl(const std::string& s)             { write_token(DOCDECL, s); }
    void ignorableWhitespace(const std::string& s)  { write_token(IGNORABLE_WHITESPACE, s); }
    void entityRef(const std::string& s)            { write_token(ENTITY_REF, s); }
};

// ---------------------------------------------------------------------------
// AbxReader — decodes binary XML to escaped human-readable XML
// ---------------------------------------------------------------------------

class AbxReader {
    std::istream& m_in;
    std::ostream& m_out;
    std::vector<std::string> m_intern;
    bool m_ok = true;
    bool m_tagOpen = false;

    uint16_t read_short() {
        char b[2];
        m_in.read(b, 2);
        if (m_in.gcount() != 2) { m_ok = false; return 0; }
        return uint16_t((uint8_t(b[0]) << 8) | uint8_t(b[1]));
    }
    uint32_t read_int() {
        char b[4];
        m_in.read(b, 4);
        if (m_in.gcount() != 4) { m_ok = false; return 0; }
        return (uint32_t(uint8_t(b[0])) << 24) | (uint32_t(uint8_t(b[1])) << 16) |
               (uint32_t(uint8_t(b[2])) << 8) | uint8_t(b[3]);
    }
    uint64_t read_long() {
        uint64_t hi = read_int();
        uint64_t lo = read_int();
        return (hi << 32) | lo;
    }
    std::string read_utf() {
        uint16_t len = read_short();
        if (!m_ok) return {};
        std::string s(len, '\0');
        if (len) {
            m_in.read(&s[0], len);
            if (m_in.gcount() != len) { m_ok = false; return {}; }
        }
        bool ok = true;
        std::string out = mutf8_to_utf8(s, ok);
        if (!ok) { m_ok = false; return {}; }
        return out;
    }
    std::string read_interned() {
        uint16_t id = read_short();
        if (!m_ok) return {};
        if (id == INTERN_SENTINEL) {
            std::string s = read_utf();
            if (!m_ok) return {};
            if (m_intern.size() < INTERN_SENTINEL) m_intern.push_back(s);
            return s;
        }
        if (id >= m_intern.size()) { m_ok = false; return {}; }
        return m_intern[id];
    }
    std::string read_value(uint8_t type) {
        switch (type) {
            case TYPE_NULL: return {};
            case TYPE_STRING:          return read_utf();
            case TYPE_STRING_INTERNED: return read_interned();
            case TYPE_BYTES_HEX:
            case TYPE_BYTES_BASE64: {
                uint16_t len = read_short();
                if (!m_ok) return {};
                std::string bytes(len, '\0');
                if (len) {
                    m_in.read(&bytes[0], len);
                    if (m_in.gcount() != len) { m_ok = false; return {}; }
                }
                return (type == TYPE_BYTES_HEX) ? to_hex_lower(bytes) : to_base64(bytes);
            }
            case TYPE_INT:
            case TYPE_INT_HEX: {
                uint32_t v = read_int();
                if (!m_ok) return {};
                return (type == TYPE_INT) ? std::to_string(static_cast<int32_t>(v))
                                          : int_to_hex(static_cast<int32_t>(v));
            }
            case TYPE_LONG:
            case TYPE_LONG_HEX: {
                uint64_t v = read_long();
                if (!m_ok) return {};
                return (type == TYPE_LONG) ? std::to_string(static_cast<int64_t>(v))
                                           : long_to_hex(static_cast<int64_t>(v));
            }
            case TYPE_FLOAT: {
                uint32_t v = read_int();
                if (!m_ok) return {};
                return format_fp(std::bit_cast<float>(v));
            }
            case TYPE_DOUBLE: {
                uint64_t v = read_long();
                if (!m_ok) return {};
                return format_fp(std::bit_cast<double>(v));
            }
            case TYPE_BOOLEAN_TRUE:  return "true";
            case TYPE_BOOLEAN_FALSE: return "false";
            default: m_ok = false; return {};
        }
    }
    void close_tag_if_open() {
        if (m_tagOpen) { m_out.put('>'); m_tagOpen = false; }
    }
    void emit_text_escaped(const std::string& s) {
        for (char c : s) {
            switch (c) {
                case '&': m_out.write("&amp;", 5); break;
                case '<': m_out.write("&lt;", 4); break;
                case '>': m_out.write("&gt;", 4); break;
                default: m_out.put(c); break;
            }
        }
    }
    void emit_attrval_escaped(const std::string& s) {
        for (char c : s) {
            switch (c) {
                case '&': m_out.write("&amp;", 5); break;
                case '<': m_out.write("&lt;", 4); break;
                case '>': m_out.write("&gt;", 4); break;
                case '"': m_out.write("&quot;", 6); break;
                default: m_out.put(c); break;
            }
        }
    }

public:
    AbxReader(std::istream& in, std::ostream& out) : m_in(in), m_out(out) {}

    bool run() {
        char magic[4];
        m_in.read(magic, 4);
        if (m_in.gcount() != 4 ||
            magic[0] != 0x41 || magic[1] != 0x42 ||
            magic[2] != 0x58 || magic[3] != 0x00) {
            m_ok = false;
            return false;
        }
        char b;
        while (m_in.get(b)) {
            uint8_t tok = static_cast<uint8_t>(b) & 0x0F;
            uint8_t ty  = static_cast<uint8_t>(b) & 0xF0;
            if (tok == ATTRIBUTE) {
                std::string name = read_interned();
                std::string val  = read_value(ty);
                m_out.put(' ');
                m_out.write(name.data(), static_cast<std::streamsize>(name.size()));
                m_out.write("=\"", 2);
                emit_attrval_escaped(val);
                m_out.put('"');
                continue;
            }
            close_tag_if_open();
            switch (tok) {
                case START_DOCUMENT: {
                    // Regenerated XML declaration (mirrors Java
                    // FastXmlSerializer.startDocument, which emits no trailing
                    // newline — any newline in the output comes from the
                    // preserved TEXT events, keeping xml->abx->xml idempotent).
                    constexpr std::string_view kDecl =
                        "<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\"?>";
                    m_out.write(kDecl.data(), static_cast<std::streamsize>(kDecl.size()));
                    break;
                }
                case END_DOCUMENT:
                    return m_ok;
                case START_TAG: {
                    std::string n = read_interned();
                    m_out.put('<');
                    m_out.write(n.data(), static_cast<std::streamsize>(n.size()));
                    m_tagOpen = true;
                    break;
                }
                case END_TAG: {
                    std::string n = read_interned();
                    m_out.write("</", 2);
                    m_out.write(n.data(), static_cast<std::streamsize>(n.size()));
                    m_out.put('>');
                    break;
                }
                case TEXT:
                    emit_text_escaped(read_value(ty));
                    break;
                case CDSECT: {
                    std::string v = read_value(ty);
                    m_out.write("<![CDATA[", 9);
                    m_out.write(v.data(), static_cast<std::streamsize>(v.size()));
                    m_out.write("]]>", 3);
                    break;
                }
                case COMMENT: {
                    std::string v = read_value(ty);
                    m_out.write("<!--", 4);
                    m_out.write(v.data(), static_cast<std::streamsize>(v.size()));
                    m_out.write("-->", 3);
                    break;
                }
                case PROCESSING_INSTRUCTION: {
                    std::string v = read_value(ty);
                    m_out.write("<?", 2);
                    m_out.write(v.data(), static_cast<std::streamsize>(v.size()));
                    m_out.write("?>", 2);
                    break;
                }
                case DOCDECL: {
                    std::string v = read_value(ty);
                    m_out.write("<!DOCTYPE ", 10);
                    m_out.write(v.data(), static_cast<std::streamsize>(v.size()));
                    m_out.put('>');
                    break;
                }
                case ENTITY_REF: {
                    std::string v = read_value(ty);
                    m_out.put('&');
                    m_out.write(v.data(), static_cast<std::streamsize>(v.size()));
                    m_out.put(';');
                    break;
                }
                case IGNORABLE_WHITESPACE:
                    emit_text_escaped(read_value(ty));
                    break;
                default:
                    m_ok = false;
                    return m_ok;
            }
        }
        return m_ok;
    }
};

// ---------------------------------------------------------------------------
// XmlParser — self-contained recursive-descent XML text parser -> AbxWriter
// events. No rapidxml. Entities are expanded into TEXT (kxml2 default; the
// encoder therefore never emits ENTITY_REF). Pure whitespace is emitted as
// TEXT (TEXT and IGNORABLE_WHITESPACE share the on-wire TYPE_STRING form and
// decode identically, so this is round-trip-safe).
// ---------------------------------------------------------------------------

class XmlParser {
    const char* m_p;
    const char* m_end;
    bool m_ok = true;

    bool eof() const { return m_p >= m_end; }
    char peek() const { return eof() ? '\0' : *m_p; }
    bool at(const char* s) const {
        size_t n = 0;
        while (s[n]) {
            if (m_p + n >= m_end || m_p[n] != s[n]) return false;
            ++n;
        }
        return true;
    }
    void skip_ws() {
        while (!eof() && (*m_p == ' ' || *m_p == '\t' ||
                          *m_p == '\n' || *m_p == '\r')) ++m_p;
    }
    static bool is_name_start(char c) {
        unsigned char u = static_cast<unsigned char>(c);
        return std::isalpha(u) || c == '_' || c == ':';
    }
    static bool is_name_char(char c) {
        unsigned char u = static_cast<unsigned char>(c);
        return std::isalnum(u) || c == '_' || c == ':' || c == '.' || c == '-';
    }
    std::string read_name() {
        std::string n;
        if (eof() || !is_name_start(*m_p)) { m_ok = false; return {}; }
        while (!eof() && is_name_char(*m_p)) n.push_back(*m_p++);
        return n;
    }
    std::string read_quoted() {
        char q = peek();
        if (q != '"' && q != '\'') { m_ok = false; return {}; }
        ++m_p;
        std::string s;
        while (!eof() && *m_p != q) s.push_back(*m_p++);
        if (eof() || *m_p != q) { m_ok = false; return {}; }
        ++m_p;
        return s;
    }
    static void append_cp_utf8(std::string& out, uint32_t cp) { append_utf8(out, cp); }
    std::string expand_entities(const std::string& s) {
        std::string out;
        size_t i = 0;
        while (i < s.size()) {
            if (s[i] == '&') {
                size_t semi = s.find(';', i + 1);
                if (semi == std::string::npos) { out.push_back(s[i++]); continue; }
                std::string ent = s.substr(i + 1, semi - i - 1);
                if (ent == "lt") out.push_back('<');
                else if (ent == "gt") out.push_back('>');
                else if (ent == "amp") out.push_back('&');
                else if (ent == "apos") out.push_back('\'');
                else if (ent == "quot") out.push_back('"');
                else if (!ent.empty() && ent[0] == '#') {
                    uint32_t cp = 0;
                    bool okp = true;
                    if (ent.size() > 1 && ent[1] == 'x') {
                        for (size_t k = 2; k < ent.size(); ++k) {
                            cp <<= 4;
                            char c = ent[k];
                            if (c >= '0' && c <= '9') cp |= uint32_t(c - '0');
                            else if (c >= 'a' && c <= 'f') cp |= uint32_t(c - 'a' + 10);
                            else if (c >= 'A' && c <= 'F') cp |= uint32_t(c - 'A' + 10);
                            else { okp = false; break; }
                        }
                    } else {
                        for (size_t k = 1; k < ent.size(); ++k) {
                            cp = cp * 10 + uint32_t(ent[k] - '0');
                        }
                    }
                    if (okp) append_cp_utf8(out, cp);
                    else out += s.substr(i, semi - i + 1);
                } else {
                    out += s.substr(i, semi - i + 1);
                }
                i = semi + 1;
            } else {
                out.push_back(s[i++]);
            }
        }
        return out;
    }

    void parse_text(AbxWriter& w) {
        std::string raw;
        while (!eof() && *m_p != '<') raw.push_back(*m_p++);
        if (!raw.empty()) w.text(expand_entities(raw));
    }
    void parse_comment(AbxWriter& w) {
        std::string inner;
        while (!eof()) {
            if (m_end - m_p >= 3 && m_p[0] == '-' && m_p[1] == '-' && m_p[2] == '>') {
                m_p += 3;
                w.comment(inner);
                return;
            }
            inner.push_back(*m_p++);
        }
        m_ok = false;
    }
    void parse_cdata(AbxWriter& w) {
        std::string inner;
        while (!eof()) {
            if (m_end - m_p >= 3 && m_p[0] == ']' && m_p[1] == ']' && m_p[2] == '>') {
                m_p += 3;
                w.cdsect(inner);
                return;
            }
            inner.push_back(*m_p++);
        }
        m_ok = false;
    }
    void parse_doctype(AbxWriter& w) {
        skip_ws();
        std::string content;
        int depth = 0;
        while (!eof()) {
            char c = *m_p;
            if (c == '[') { ++depth; content.push_back(c); ++m_p; }
            else if (c == ']') { --depth; content.push_back(c); ++m_p; }
            else if (c == '>' && depth == 0) { ++m_p; w.docdecl(content); return; }
            else { content.push_back(c); ++m_p; }
        }
        m_ok = false;
    }
    void parse_pi(AbxWriter& w) {
        // kxml2 yields the entire text between <? and ?> as the PI content
        // (target included), so store it verbatim for round-trip fidelity.
        std::string content;
        while (!eof()) {
            if (m_end - m_p >= 2 && m_p[0] == '?' && m_p[1] == '>') { m_p += 2; break; }
            content.push_back(*m_p++);
        }
        // <?xml ...?> is the document declaration (target "xml" is reserved);
        // kxml2 consumes it as the document start, so skip it here — START_DOCUMENT
        // is emitted unconditionally by encode_abx.
        bool isDecl = content.size() >= 3 &&
            content[0] == 'x' && content[1] == 'm' && content[2] == 'l' &&
            (content.size() == 3 ||
             content[3] == ' ' || content[3] == '\t' ||
             content[3] == '\n' || content[3] == '\r');
        if (!isDecl) w.pi(content);
    }
    void parse_element(AbxWriter& w) {
        std::string name = read_name();
        if (!m_ok) return;
        w.startTag(name);
        while (m_ok) {
            skip_ws();
            char c = peek();
            if (c == '>' || c == '/' || eof()) break;
            std::string aname = read_name();
            if (!m_ok) return;
            skip_ws();
            if (peek() != '=') { m_ok = false; return; }
            ++m_p;
            skip_ws();
            std::string raw = read_quoted();
            if (!m_ok) return;
            w.attribute(aname, expand_entities(raw));
        }
        if (peek() == '/') {
            ++m_p;
            if (peek() == '>') { ++m_p; w.endTag(name); return; }
            m_ok = false; return;
        }
        if (peek() == '>') ++m_p;
        else { m_ok = false; return; }
        // children
        while (m_ok && !eof()) {
            if (*m_p == '<') {
                ++m_p;
                char c = peek();
                if (c == '/') {
                    ++m_p;
                    std::string ename = read_name();
                    if (!m_ok || ename != name) { m_ok = false; return; }
                    skip_ws();
                    if (peek() != '>') { m_ok = false; return; }
                    ++m_p;
                    w.endTag(name);
                    return;
                } else if (c == '!') {
                    ++m_p;
                    if (at("--")) { m_p += 2; parse_comment(w); }
                    else if (at("[CDATA[")) { m_p += 7; parse_cdata(w); }
                    else { m_ok = false; }
                } else if (c == '?') {
                    ++m_p; parse_pi(w);
                } else {
                    parse_element(w);
                }
            } else {
                parse_text(w);
            }
        }
        m_ok = false; // EOF before matching end tag
    }

public:
    XmlParser(const char* p, size_t n) : m_p(p), m_end(p + n) {}

    int run(AbxWriter& w) {
        if (m_end - m_p >= 3 &&
            static_cast<uint8_t>(m_p[0]) == 0xEF &&
            static_cast<uint8_t>(m_p[1]) == 0xBB &&
            static_cast<uint8_t>(m_p[2]) == 0xBF) {
            m_p += 3; // BOM
        }
        while (m_ok && !eof()) {
            if (*m_p == '<') {
                ++m_p;
                if (eof()) { m_ok = false; break; }
                char c = *m_p;
                if (c == '?') { ++m_p; parse_pi(w); }
                else if (c == '!') {
                    ++m_p;
                    if (at("--")) { m_p += 2; parse_comment(w); }
                    else if (at("[CDATA[")) { m_p += 7; parse_cdata(w); }
                    else if (at("DOCTYPE")) { m_p += 7; parse_doctype(w); }
                    else { m_ok = false; }
                } else if (c == '/') {
                    m_ok = false; // stray end tag at top level
                } else {
                    parse_element(w);
                }
            } else {
                parse_text(w);
            }
        }
        return m_ok ? 0 : 1;
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Core stream-to-stream conversions (declared in abx.hpp)
//
// The XmlParser / AbxWriter / AbxReader helpers above live in the anonymous
// namespace but remain visible to the rest of the translation unit, so these
// definitions sit outside it and expose external linkage matching the
// abx.hpp declarations for programmatic callers.
// ---------------------------------------------------------------------------

int encode_abx(std::istream& in, std::ostream& out) {
    std::string xml((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    XmlParser ps(xml.data(), xml.size());
    AbxWriter w(out);
    w.startDocument();
    int r = ps.run(w);
    w.endDocument();
    return (r == 0 && w.ok()) ? 0 : 1;
}

int decode_abx(std::istream& in, std::ostream& out) {
    AbxReader r(in, out);
    return r.run() ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Direction functions derived from Abx.java mainInternal():
//  - input  == "-" => stdin
//  - output == "-" => stdout
//  - in_place: output written to input + ".tmp", renamed over input on
//    success, unlinked on failure. stdin cannot be used in-place.
// ---------------------------------------------------------------------------

static int do_convert(bool encode, const std::string& input,
                      const std::string& output, bool in_place) {
    const std::string outPath = in_place ? (input + ".tmp") : output;

    std::ifstream fin;
    std::ofstream fout;
    if (input != "-") {
        fin.open(input, std::ios::binary);
        if (!fin.is_open()) return 1;
    }
    if (outPath != "-") {
        fout.open(outPath, std::ios::binary);
        if (!fout.is_open()) {
            if (in_place) ::unlink(outPath.c_str());
            return 1;
        }
    }
    std::istream& is = (input == "-") ? static_cast<std::istream&>(std::cin)  : fin;
    std::ostream& os = (outPath == "-") ? static_cast<std::ostream&>(std::cout) : fout;

    int r = encode ? encode_abx(is, os) : decode_abx(is, os);
    os.flush();
    if (r != 0) {
        if (in_place) ::unlink(outPath.c_str());
        return 1;
    }
    if (in_place) {
        if (::rename(outPath.c_str(), input.c_str()) != 0) return 1;
    }
    return 0;
}

int abx2xml(const std::string& input, const std::string& output, bool in_place) {
    return do_convert(/*encode=*/false, input, output, in_place);
}

int xml2abx(const std::string& input, const std::string& output, bool in_place) {
    return do_convert(/*encode=*/true, input, output, in_place);
}
