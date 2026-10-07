// An independent reader for the MOO v1.1 test file format that
// SingleStepTests publishes (a chunked little-endian binary; see
// https://github.com/dbalsom/moo/blob/main/doc/moo_format_v1.md and
// docs/kb/singlesteptests-moo.md). Written from the specification; no
// upstream parser code is copied.
//
// Only what the core's validation needs is materialized: the header, META,
// and each test's name, instruction bytes, initial/final register and RAM
// state, undefined-state masks and exception record. CYCL (bus cycles) and
// QUEU (prefetch queue) are skipped by their length fields, as the spec
// directs for chunks a parser does not use.

#ifndef REX86_TOOLS_SST_MOO_READER_H_
#define REX86_TOOLS_SST_MOO_READER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rex86::sst
{

// The RG32/RM32 register file, in the bitfield order the spec gives
// (bit 0 = cr0 ... bit 19 = dr7). `present` says which values were stored:
// a FINA chunk carries only the registers the instruction changed.
struct RegisterSet32
{
    static constexpr std::size_t kCount = 20;
    std::uint32_t present = 0;
    std::uint32_t values[kCount] = {};

    bool Has(const std::size_t index) const
    {
        return index < kCount && (present >> index & 1U) != 0;
    }
};

// Indices into RegisterSet32, from the spec's bitfield table.
enum Reg32Index : std::size_t
{
    kRegCr0 = 0, kRegCr3, kRegEax, kRegEbx, kRegEcx, kRegEdx, kRegEsi,
    kRegEdi, kRegEbp, kRegEsp, kRegCs, kRegDs, kRegEs, kRegFs, kRegGs,
    kRegSs, kRegEip, kRegEflags, kRegDr6, kRegDr7,
};

struct RamEntry
{
    std::uint32_t address = 0;
    std::uint8_t value = 0;
};

struct CpuStateChunk
{
    RegisterSet32 regs;
    RegisterSet32 masks;  // RM32: undefined-state masks, FINA only
    bool has_masks = false;
    std::vector<RamEntry> ram;
};

struct ExceptionRecord
{
    std::uint8_t number = 0;
    std::uint32_t flag_address = 0;
};

struct MooTest
{
    std::uint32_t index = 0;
    std::string name;
    std::vector<std::uint8_t> bytes;
    CpuStateChunk initial;
    CpuStateChunk final_state;
    bool has_exception = false;
    ExceptionRecord exception;
};

struct MooFile
{
    std::uint8_t version_major = 0;
    std::uint8_t version_minor = 0;
    std::uint32_t declared_test_count = 0;
    std::string cpu_id;        // e.g. "386E"
    std::string mnemonic;      // META, trimmed of padding
    std::uint32_t opcode = 0;  // META; 0xFFFFFFFF means mixed
    std::uint8_t cpu_mode = 0; // META; 0 is real mode
    RegisterSet32 file_masks;  // top-level RM32, applies to every test
    bool has_file_masks = false;
    std::vector<MooTest> tests;
};

// Parses a whole MOO file image. Returns false and fills `error` on
// malformed input; unknown chunks are skipped, not errors.
bool ParseMooFile(const std::uint8_t* data, std::size_t size, MooFile* out,
                  std::string* error);

}  // namespace rex86::sst

#endif  // REX86_TOOLS_SST_MOO_READER_H_
