// jsp_mp_pack.h -- the packed form of the two-page Main Menu pictures (mp_pic_<lang>.inc, made by installer/tools/mp_label_langs.py).
// Why: the pictures are DXT5 blocks, mostly empty or repeated; as C arrays they were 8.6 MB of source. Packed: the blob (8 row labels, the page title, the
// watermark; 269440 bytes) is cut into 16-byte blocks and run-length coded, and the result is written as base 85 text in a string literal (~1.25 chars/byte).
//   RLE op byte b: b < 0x80: the next (b + 1) blocks are literal; b >= 0x80: the next ONE block repeats (b & 0x7F) + 1 times.
//   Stream: u32 little-endian length of the RLE bytes, then the RLE bytes, as base 85: 4 bytes (big endian) -> 5 digits, the last group of n bytes -> n + 1 digits.
//   Alphabet: the 85 printable ASCII characters from '!' on, without '"', '\\', '?' and '@' (a string literal needs no escape, no trigraph; no '@' so that the repo's e-mail check never reads the text as an address).
// Header only; installer/tools/menu_lang_test.cpp tests it (round trip, a golden vector made by the Python tool, malformed input).
#ifndef JSP_MP_PACK_H
#define JSP_MP_PACK_H
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace jmp {
inline const std::string& alphabet()
{
    static const std::string a = [] { std::string s; for (int c = 33; c < 127 && s.size() < 85; c++) if (c != '"' && c != '\\' && c != '?' && c != '@') s += (char)c; return s; }();
    return a;
}
inline std::vector<uint8_t> rle(const std::vector<uint8_t>& blob)   // blob size must be a multiple of 16
{
    std::vector<uint8_t> o; const size_t nb = blob.size() / 16; size_t i = 0;
    auto same = [&](size_t a, size_t b) { return !std::memcmp(&blob[a * 16], &blob[b * 16], 16); };
    while (i < nb) {
        size_t r = 1; while (i + r < nb && r < 128 && same(i, i + r)) r++;
        if (r >= 2) { o.push_back((uint8_t)(0x80 | (r - 1))); o.insert(o.end(), blob.begin() + i * 16, blob.begin() + (i + 1) * 16); i += r; continue; }
        size_t l = 1; while (i + l < nb && l < 128 && !(i + l + 1 < nb && same(i + l, i + l + 1))) l++;
        o.push_back((uint8_t)(l - 1)); o.insert(o.end(), blob.begin() + i * 16, blob.begin() + (i + l) * 16); i += l;
    }
    return o;
}
// returns an empty vector on malformed input (a run past the output size, a missing block, a size that is not out_bytes)
inline std::vector<uint8_t> unrle(const uint8_t* p, size_t n, size_t out_bytes)
{
    std::vector<uint8_t> o; o.reserve(out_bytes); size_t i = 0;
    while (i < n) {
        const uint8_t b = p[i++]; const size_t cnt = (b & 0x7F) + 1u;
        if (b < 0x80) { if (i + cnt * 16 > n || o.size() + cnt * 16 > out_bytes) return {}; o.insert(o.end(), p + i, p + i + cnt * 16); i += cnt * 16; }
        else { if (i + 16 > n || o.size() + cnt * 16 > out_bytes) return {}; for (size_t k = 0; k < cnt; k++) o.insert(o.end(), p + i, p + i + 16); i += 16; }
    }
    if (o.size() != out_bytes) return {};
    return o;
}
inline std::string b85(const std::vector<uint8_t>& d)
{
    const std::string& A = alphabet(); std::string s;
    for (size_t i = 0; i < d.size(); i += 4) {
        const size_t n = d.size() - i < 4 ? d.size() - i : 4; uint32_t v = 0; for (size_t k = 0; k < 4; k++) v = (v << 8) | (k < n ? d[i + k] : 0u);
        char t[5]; for (int k = 4; k >= 0; k--) { t[k] = A[v % 85]; v /= 85; }
        s.append(t, n + 1);
    }
    return s;
}
inline bool unb85(const char* s, size_t len, std::vector<uint8_t>& out)
{
    const std::string& A = alphabet(); int val[256]; for (int i = 0; i < 256; i++) val[i] = -1; for (size_t i = 0; i < A.size(); i++) val[(uint8_t)A[i]] = (int)i;
    out.clear();
    for (size_t i = 0; i < len;) {
        const size_t n = len - i < 5 ? len - i : 5; if (n < 2) return false;
        uint64_t v = 0; for (size_t k = 0; k < 5; k++) { int d = 84; if (k < n) { d = val[(uint8_t)s[i + k]]; if (d < 0) return false; } v = v * 85 + (uint64_t)d; }
        if (v > 0xFFFFFFFFull) return false;
        for (size_t k = 0; k + 1 < n; k++) out.push_back((uint8_t)(v >> (24 - 8 * k)));
        i += n;
    }
    return true;
}
inline std::string pack(const std::vector<uint8_t>& blob)
{
    std::vector<uint8_t> r = rle(blob), d; const uint32_t n = (uint32_t)r.size();
    for (int k = 0; k < 4; k++) d.push_back((uint8_t)(n >> (8 * k)));
    d.insert(d.end(), r.begin(), r.end());
    return b85(d);
}
// text -> the blob of out_bytes bytes; empty on any error
inline std::vector<uint8_t> unpack(const char* text, size_t out_bytes)
{
    std::vector<uint8_t> d; if (!text || !unb85(text, std::strlen(text), d) || d.size() < 4) return {};
    const uint32_t n = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
    if (d.size() != 4u + (size_t)n) return {};
    return unrle(d.data() + 4, n, out_bytes);
}
}  // namespace jmp
#endif
