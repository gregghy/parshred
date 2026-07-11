/// @file fuzz_sax.cpp
/// @brief libFuzzer harness for the fast single-pass SAX parser.
///
/// The fuzzer feeds arbitrary byte sequences to fast_parse() with a
/// NullHandler.  The contract being checked is purely safety:
///   - no crashes
///   - no hangs (libFuzzer enforces this via -timeout)
///   - no sanitizer findings (ASan/UBSan are enabled by the CMake target)
///
/// Build (manual):
///   clang++ -std=c++20 -fsanitize=fuzzer,address,undefined \
///           -I../../include fuzz_sax.cpp ../../build/fuzz/libparshred.a \
///           -o fuzz_sax
///
/// Run:
///   ./fuzz_sax corpus/ -max_total_time=60

#include <parshred/fast_sax.hpp>

#include <cstdint>
#include <cstddef>

/// libFuzzer entry point.
///
/// Every byte sequence libFuzzer generates is handed to both ParseMode
/// variants so that we exercise full entity expansion and turbo mode in
/// one harness invocation.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Cast to the char pointer the parser API expects.  The data is read-only;
    // fast_parse() in Normal mode does not mutate the input buffer.
    const char* xml = reinterpret_cast<const char*>(data);

    // ── Normal mode (entity expansion, depth tracking, stats) ────────────
    {
        parshred::NullHandler handler;
        try {
            parshred::fast_parse(xml, size, handler);
        } catch (const parshred::ParseError&) {
            // Expected for malformed input — not a bug.
        } catch (const parshred::SecurityError&) {
            // Depth/entity limit triggered — expected on adversarial input.
        } catch (const std::exception&) {
            // Any other std::exception is fine; re-throw anything else so
            // the fuzzer can detect truly unexpected terminations.
        }
    }

    // ── Turbo mode (no entity expansion, no depth check) ─────────────────
    {
        parshred::NullHandler handler;
        try {
            parshred::fast_parse_turbo(xml, size, handler);
        } catch (const parshred::ParseError&) {
        } catch (const std::exception&) {
        }
    }

    return 0;
}
