// The trace format (design #22, decision 5): a record of "this input must
// produce this result". A host CPU produces the expectation, and the core on
// any host replays it. The format is little-endian binary so a file written
// on x86 replays bit for bit on AArch64, wasm32 and Windows.
//
//   file: "RX86TRC1" | u32 version | u32 case_count | case...
//   case: u32 size, then the fields of Case in declaration order.
//
// Region contents start as an xorshift32 sequence (FillRegion), so a case
// carries only the bytes that differ from it (patches in, diffs out).

#ifndef REX86_TRACE_TRACE_FORMAT_H_
#define REX86_TRACE_TRACE_FORMAT_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace rex86::trace
{

inline constexpr std::uint32_t kFormatVersion = 1;

enum class CaseKind : std::uint8_t
{
    kInteger32 = 1,  // one integer instruction in 32-bit flat code
    kX87 = 2,        // the x87 fuzz's stub, run until HLT
    kSimd = 3,       // the SIMD fuzz's FXRSTOR/FXSAVE stub, run until HLT
};

enum class RunMode : std::uint8_t
{
    kSingleStep = 0,  // one instruction: Cpu::Step until none is in progress
    kUntilHalt = 1,   // Cpu::Run(budget)
};

// Case::flags.
inline constexpr std::uint16_t kCompareFaultAddress = 1u << 0;
inline constexpr std::uint16_t kCompareEip = 1u << 1;

// Case::features, one bit per rex86::Features member.
inline constexpr std::uint32_t kFeatureX87 = 1u << 0;
inline constexpr std::uint32_t kFeatureMmx = 1u << 1;
inline constexpr std::uint32_t kFeatureSse = 1u << 2;
inline constexpr std::uint32_t kFeatureSse2 = 1u << 3;
inline constexpr std::uint32_t kFeatureSegments16 = 1u << 4;
inline constexpr std::uint32_t kFeatureCmov = 1u << 5;
inline constexpr std::uint32_t kFeatureFxsr = 1u << 6;

struct Region
{
    std::uint32_t base = 0;
    std::uint32_t size = 0;
    std::uint8_t page_flags = 0;  // rex86::PageFlag bits
};

struct Bytes
{
    std::uint32_t address = 0;
    std::vector<std::uint8_t> bytes;
};

struct Range
{
    std::uint32_t address = 0;
    std::uint16_t length = 0;
};

struct Registers
{
    std::array<std::uint32_t, 8> gpr = {};
    std::uint32_t eip = 0;
    std::uint32_t eflags = 0;
};

struct Case
{
    CaseKind kind = CaseKind::kInteger32;
    RunMode mode = RunMode::kSingleStep;
    std::uint16_t flags = 0;
    std::uint32_t features = 0;

    // Input.
    std::vector<Region> regions;
    std::uint32_t fill_seed = 0;
    std::vector<Bytes> patches;
    // Selectors in rex86::Segment order (ES, CS, SS, DS, FS, GS). A present
    // segment is flat (base 0, 4 GiB limit, 32-bit); an absent one is a null
    // selector that faults on use.
    std::array<std::uint16_t, 6> selectors = {};
    std::uint8_t segment_present = 0x3Fu;
    Registers input;
    std::uint32_t budget = 1;

    // Expected.
    std::uint8_t reason = 0;      // rex86::StopReason
    std::uint8_t fault_kind = 0;  // rex86::FaultKind
    std::uint32_t fault_address = 0;
    Registers expected;
    std::uint32_t eflags_mask = 0xFFFFFFFFu;
    std::uint8_t gpr_mask = 0xFFu;  // bit i: compare gpr[i]
    std::vector<Bytes> diffs;       // changes against the input memory
    std::vector<Range> ignores;     // bytes not compared
};

// The initial contents of a region: an xorshift32 sequence seeded with
// fill_seed ^ base (a zero seed becomes 0x9E3779B9), four bytes per step,
// little-endian.
void FillRegion(std::uint32_t fill_seed, std::uint32_t base, std::uint8_t* out,
                std::size_t size);

void EncodeCase(const Case& value, std::vector<std::uint8_t>* out);
// Decodes one case starting at data; on success *consumed is its size.
bool DecodeCase(const std::uint8_t* data, std::size_t size, Case* value,
                std::size_t* consumed, std::string* error);

// Writes cases as they come and fixes the count in the header on Close.
class Writer
{
public:
    Writer() = default;
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    ~Writer();

    bool Open(const std::string& path, std::string* error);
    bool Add(const Case& value);
    bool Close();
    std::uint32_t count() const
    {
        return count_;
    }

private:
    std::FILE* file_ = nullptr;
    std::uint32_t count_ = 0;
};

bool ReadFile(const std::string& path, std::vector<Case>* cases,
              std::string* error);

// A readable listing of one case, for --dump and failure reports.
std::string Describe(const Case& value);

}  // namespace rex86::trace

#endif  // REX86_TRACE_TRACE_FORMAT_H_
