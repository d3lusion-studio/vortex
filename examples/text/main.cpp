// Headless: the UTF-8 decoder every string in the engine is read through.
//
// No window and no GPU, so it is a CI check. This is worth pinning because the failure
// mode is invisible: a decoder that drops a byte does not crash, it renders "Nng tri"
// instead of "Nông trại" and nobody notices until someone writes in their own language.
// That is exactly the bug this replaced.

#include "vortex/text/font.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace vortex;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// Decode a whole string into codepoints, the way drawText walks it.
std::vector<u32> decodeAll(std::string_view s) {
    std::vector<u32> out;
    for (usize i = 0; i < s.size();) out.push_back(text::decodeUtf8(s, i));
    return out;
}

// Every decode must advance the index, or a malformed byte becomes an infinite loop in the
// middle of the render path.
bool alwaysAdvances(std::string_view s) {
    usize i = 0;
    for (int guard = 0; i < s.size(); ++guard) {
        const usize before = i;
        text::decodeUtf8(s, i);
        if (i <= before) return false;
        if (guard > 64) return false;
    }
    return true;
}

constexpr u32 kReplacement = 0xFFFDu;

}   // namespace

int main() {
    std::printf("UTF-8 decoding\n\n");

    // --- ASCII is one byte per codepoint ------------------------------------------------
    {
        const std::vector<u32> cps = decodeAll("Hi 123");
        check(cps.size() == 6 && cps[0] == 'H' && cps[5] == '3', "ascii: one codepoint per byte");
    }

    // --- The case that was broken -------------------------------------------------------
    //
    // "Nông trại" is 9 codepoints but 12 bytes: ô and ạ are two and three bytes. Reading it
    // a `char` at a time yields 12 lookups, 3 of which land on continuation bytes.
    {
        const std::string_view s = "Nông trại";
        const std::vector<u32> cps = decodeAll(s);
        check(s.size() == 12, "vietnamese: 12 bytes on the wire");
        check(cps.size() == 9, "vietnamese: 9 codepoints out");
        check(text::utf8Length(s) == 9, "vietnamese: utf8Length agrees");
        check(cps[1] == 0x00F4, "vietnamese: 'o' with circumflex decodes to U+00F4");
        check(cps[7] == 0x1EA1, "vietnamese: 'a' with dot below decodes to U+1EA1");
    }

    // --- Two, three and four byte sequences ---------------------------------------------
    {
        check(decodeAll("é")[0] == 0x00E9, "2-byte: U+00E9");
        check(decodeAll("—")[0] == 0x2014, "3-byte: em dash U+2014");
        check(decodeAll("😀")[0] == 0x1F600, "4-byte: U+1F600 beyond the BMP");
        check(text::utf8Length("aé—😀") == 4, "mixed widths count as 4 codepoints");
    }

    // --- Malformed input yields U+FFFD and keeps going -----------------------------------
    //
    // Not a crash and not a silent skip: a replacement character is VISIBLE, which is how a
    // mangled string reports itself instead of quietly losing letters.
    {
        check(decodeAll(std::string("\x80"))[0] == kReplacement, "stray continuation byte");
        check(decodeAll(std::string("\xFF"))[0] == kReplacement, "invalid lead byte");

        // A 3-byte lead with only one continuation: truncated.
        check(decodeAll(std::string("\xE2\x82"))[0] == kReplacement, "truncated sequence");

        // A lead followed by a non-continuation: broken, and the good byte after it must
        // still be read rather than swallowed with the bad one.
        const std::vector<u32> cps = decodeAll(std::string("\xC3" "A"));
        check(cps.size() == 2 && cps[0] == kReplacement && cps[1] == 'A',
              "broken sequence resyncs onto the next byte");
    }

    // --- Overlong encodings are rejected --------------------------------------------------
    //
    // 0xC0 0x80 spells U+0000 in two bytes; 0xE0 0x80 0xAF spells '/' in three. Both are
    // well-formed-looking and both are illegal — the standard says a codepoint has exactly
    // one encoding. Accepting them is how a NUL or a '/' smuggles itself past a filter that
    // checked the bytes.
    {
        check(decodeAll(std::string("\xC0\x80"))[0] == kReplacement, "overlong NUL rejected");
        check(decodeAll(std::string("\xE0\x80\xAF"))[0] == kReplacement, "overlong '/' rejected");
        check(decodeAll(std::string("\xF0\x80\x80\xAF"))[0] == kReplacement,
              "overlong 4-byte rejected");
    }

    // --- A surrogate half is not a codepoint ---------------------------------------------
    {
        // U+D800 encoded as if it were a normal 3-byte sequence. Valid-looking bytes,
        // illegal value.
        check(decodeAll(std::string("\xED\xA0\x80"))[0] == kReplacement, "surrogate rejected");
    }

    // --- No input can hang the render path -------------------------------------------------
    {
        check(alwaysAdvances("Nông trại — ok"), "advance: valid text terminates");
        check(alwaysAdvances(std::string("\x80\xFF\xC3\xE2\x82")), "advance: garbage terminates");
        check(alwaysAdvances(std::string("\xF0\x9F\x98")), "advance: truncated 4-byte terminates");
    }

    // --- The ranges the default font bakes --------------------------------------------------
    {
        const auto& ranges = text::latinRanges();
        usize total = 0;
        for (const auto& r : ranges) total += r.count;
        check(total > 400, "latinRanges: covers more than ascii");

        const auto covered = [&](u32 cp) {
            for (const auto& r : ranges)
                if (cp >= r.first && cp < r.first + r.count) return true;
            return false;
        };
        check(covered('A') && covered('~'), "latinRanges: basic latin");
        check(covered(0x00F4), "latinRanges: U+00F4 o-circumflex");
        check(covered(0x1EA1), "latinRanges: U+1EA1 a-dot-below");
        check(covered(0x01B0), "latinRanges: U+01B0 u-horn");
        check(covered(0x2014), "latinRanges: U+2014 em dash");
    }

    std::printf("\n[%s] Text self-check: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL",
                failures);
    return failures == 0 ? 0 : 1;
}
