#include "trace/replay.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "rex86/cpu.h"
#include "rex86/environment.h"
#include "rex86/guest_memory.h"

namespace rex86::trace
{
namespace
{

// Trace cases load no segment registers and use no ports or interrupts;
// anything that reaches the host is answered plainly and shows up as a
// mismatch against the recorded outcome.
class ReplayEnvironment final : public Environment
{
public:
    bool LoadDescriptor(std::uint16_t, Descriptor* descriptor) override
    {
        *descriptor = Descriptor{};
        return true;
    }
    bool PortRead(std::uint16_t, std::uint8_t, std::uint32_t*) override
    {
        return false;
    }
    bool PortWrite(std::uint16_t, std::uint8_t, std::uint32_t) override
    {
        return false;
    }
    bool InterruptTarget(std::uint8_t, std::uint16_t*, std::uint32_t*) override
    {
        return false;
    }
    std::uint64_t ReadTimeStampCounter() override
    {
        return 0;
    }
    void Cpuid(std::uint32_t, std::uint32_t, std::uint32_t registers[4]) override
    {
        registers[0] = registers[1] = registers[2] = registers[3] = 0;
    }
};

Features ToFeatures(const std::uint32_t bits)
{
    Features features;
    features.x87 = (bits & kFeatureX87) != 0;
    features.mmx = (bits & kFeatureMmx) != 0;
    features.sse = (bits & kFeatureSse) != 0;
    features.sse2 = (bits & kFeatureSse2) != 0;
    features.segments_16bit = (bits & kFeatureSegments16) != 0;
    features.cmov = (bits & kFeatureCmov) != 0;
    features.fxsr = (bits & kFeatureFxsr) != 0;
    return features;
}

std::uint32_t MemoryEnd(const Case& value)
{
    std::uint32_t end = 0;
    for (const Region& region : value.regions)
    {
        end = std::max(end, region.base + region.size);
    }
    return end;
}

bool Ignored(const Case& value, const std::uint32_t address)
{
    for (const Range& range : value.ignores)
    {
        if (address >= range.address && address - range.address < range.length)
        {
            return true;
        }
    }
    return false;
}

std::string Format(const char* format, const std::uint32_t a, const std::uint32_t b,
                   const std::uint32_t c = 0)
{
    char text[160];
    std::snprintf(text, sizeof text, format, a, b, c);
    return text;
}

}  // namespace

ReplayResult Replay(const Case& value)
{
    ReplayResult result;
    const std::uint32_t end = MemoryEnd(value);
    std::vector<std::uint8_t> buffer(end, 0);
    GuestMemory memory(buffer.data(), end);
    for (const Region& region : value.regions)
    {
        FillRegion(value.fill_seed, region.base, buffer.data() + region.base,
                   region.size);
        memory.pages().Set(region.base, region.size,
                           static_cast<PageFlag>(region.page_flags));
    }
    for (const Bytes& patch : value.patches)
    {
        if (patch.address + patch.bytes.size() > end)
        {
            result.difference = "patch outside the regions";
            return result;
        }
        std::copy(patch.bytes.begin(), patch.bytes.end(),
                  buffer.begin() + patch.address);
    }
    std::vector<std::uint8_t> expected_memory = buffer;
    for (const Bytes& diff : value.diffs)
    {
        if (diff.address + diff.bytes.size() > end)
        {
            result.difference = "diff outside the regions";
            return result;
        }
        std::copy(diff.bytes.begin(), diff.bytes.end(),
                  expected_memory.begin() + diff.address);
    }

    ReplayEnvironment environment;
    Cpu cpu(&memory, &environment, ToFeatures(value.features));
    CpuState& state = cpu.state();
    for (std::size_t i = 0; i < 6; ++i)
    {
        SegmentRegister& segment = state.segments[i];
        segment = SegmentRegister{};
        segment.selector = value.selectors[i];
        if ((value.segment_present & (1u << i)) == 0)
        {
            // A null selector: not present and not flat, so any use faults.
            segment.present = false;
            segment.limit = 0;
            segment.writable = false;
            continue;
        }
        if (static_cast<Segment>(i) == Segment::kCs)
        {
            segment.executable = true;
            segment.writable = false;
        }
    }
    state.gpr = value.input.gpr;
    state.eip = value.input.eip;
    state.eflags = value.input.eflags;

    const Event event = value.mode == RunMode::kSingleStep
                            ? cpu.Step()
                            : cpu.Run(value.budget);

    if (static_cast<std::uint8_t>(event.reason) != value.reason)
    {
        result.difference = Format("reason expected=%u core=%u (fault core=%u)",
                                   value.reason,
                                   static_cast<std::uint32_t>(event.reason),
                                   static_cast<std::uint32_t>(event.fault_kind));
        return result;
    }
    if (event.reason == StopReason::kFault)
    {
        if (static_cast<std::uint8_t>(event.fault_kind) != value.fault_kind)
        {
            result.difference = Format("fault expected=%u core=%u", value.fault_kind,
                                       static_cast<std::uint32_t>(event.fault_kind));
            return result;
        }
        if ((value.flags & kCompareFaultAddress) != 0 &&
            event.fault_address != value.fault_address)
        {
            result.difference = Format("fault address expected=%08X core=%08X",
                                       value.fault_address, event.fault_address);
            return result;
        }
    }
    static const char* const kNames[8] = {"eax", "ecx", "edx", "ebx",
                                          "esp", "ebp", "esi", "edi"};
    for (std::size_t i = 0; i < 8; ++i)
    {
        if ((value.gpr_mask & (1u << i)) != 0 &&
            state.gpr[i] != value.expected.gpr[i])
        {
            result.difference = std::string(kNames[i]) +
                Format(" expected=%08X core=%08X", value.expected.gpr[i], state.gpr[i]);
            return result;
        }
    }
    if ((value.flags & kCompareEip) != 0 && state.eip != value.expected.eip)
    {
        result.difference = Format("eip expected=%08X core=%08X", value.expected.eip,
                                   state.eip);
        return result;
    }
    if (((state.eflags ^ value.expected.eflags) & value.eflags_mask) != 0)
    {
        result.difference = Format("eflags expected=%08X core=%08X mask=%08X",
                                   value.expected.eflags, state.eflags,
                                   value.eflags_mask);
        return result;
    }
    for (const Region& region : value.regions)
    {
        for (std::uint32_t a = region.base; a < region.base + region.size; ++a)
        {
            if (buffer[a] != expected_memory[a] && !Ignored(value, a))
            {
                result.difference = Format("memory[%08X] expected=%02X core=%02X", a,
                                           expected_memory[a], buffer[a]);
                return result;
            }
        }
    }
    result.matched = true;
    return result;
}

}  // namespace rex86::trace
