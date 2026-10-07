#include "tools/census/census.h"

#include "decode/decoder.h"

#include <algorithm>

namespace rex86::census
{

namespace
{

using rex86::decode::ControlFlow;
using rex86::decode::DecodedInstruction;
using rex86::decode::Decoder;

bool InRanges(const std::vector<ExecRange>& ranges, const std::uint32_t address)
{
    for (const ExecRange& range : ranges)
    {
        if (address >= range.start && address - range.start < range.length)
        {
            return true;
        }
    }
    return false;
}

void Tally(const DecodedInstruction& decoded, CensusResult* result)
{
    ++result->reached_instructions;

    const std::string mnemonic = decoded.MnemonicName();
    const std::string form = mnemonic + " " + decoded.OperandSignature();
    MnemonicTally& tally = result->mnemonics[mnemonic];
    ++tally.instructions;
    tally.forms.insert(form);
    ++result->form_counts[form];
    ++result->isa_sets[decoded.IsaSetName()];

    if (!decoded.IsX87())
    {
        return;
    }
    X87Tally& x87 = result->x87;
    ++x87.total;
    ++x87.mnemonics[mnemonic];
    if (decoded.HasFloat80MemoryOperand())
    {
        ++x87.float80_memory_operands;
    }
    switch (decoded.instruction.mnemonic)
    {
        case ZYDIS_MNEMONIC_FLDCW:
        case ZYDIS_MNEMONIC_FNSTCW:
            ++x87.control_word_access;
            break;
        case ZYDIS_MNEMONIC_FNSAVE:
        case ZYDIS_MNEMONIC_FRSTOR:
        case ZYDIS_MNEMONIC_FNSTENV:
        case ZYDIS_MNEMONIC_FLDENV:
            ++x87.environment_save_restore;
            break;
        default:
            break;
    }
}

// The lower bound. Follows direct jmp/call/jcc targets and fallthrough.
// Indirect calls and software interrupts continue at their fallthrough on
// the premise that they return: an INT 21h is serviced by the consumer's HLE
// and execution carries on, so stopping there would measure the tool, not
// the guest. Returns, indirect jumps, hlt and decode failures end the chain.
void WalkReachable(const std::uint8_t* image, const std::size_t image_size,
                   const CensusOptions& options,
                   const std::vector<ExecRange>& ranges, CensusResult* result)
{
    const Decoder decoder;
    std::set<std::uint32_t> visited;
    std::vector<std::uint32_t> pending = options.entries;

    const auto push = [&](const std::uint32_t address) {
        if (!InRanges(ranges, address))
        {
            ++result->out_of_range_edges;
            return;
        }
        pending.push_back(address);
    };

    while (!pending.empty())
    {
        const std::uint32_t address = pending.back();
        pending.pop_back();
        if (!visited.insert(address).second)
        {
            continue;
        }
        if (!InRanges(ranges, address))
        {
            ++result->out_of_range_edges;
            continue;
        }
        const std::uint32_t offset = address - options.base;
        if (offset >= image_size)
        {
            ++result->out_of_range_edges;
            continue;
        }

        DecodedInstruction decoded;
        if (!decoder.Decode(image + offset, image_size - offset, address,
                            &decoded))
        {
            ++result->decode_stops;
            continue;
        }
        Tally(decoded, result);

        const std::uint32_t next = address + decoded.Length();
        std::uint32_t target = 0;
        switch (decoded.Flow())
        {
            case ControlFlow::kNone:
            case ControlFlow::kIndirectCall:
            case ControlFlow::kSoftwareInterrupt:
                push(next);
                break;
            case ControlFlow::kDirectJump:
                if (decoded.DirectTarget(&target))
                {
                    push(target);
                }
                break;
            case ControlFlow::kDirectCall:
                if (decoded.DirectTarget(&target))
                {
                    push(target);
                }
                push(next);
                break;
            case ControlFlow::kConditionalBranch:
                if (decoded.DirectTarget(&target))
                {
                    push(target);
                }
                push(next);
                break;
            case ControlFlow::kReturn:
            case ControlFlow::kIndirectJump:
            case ControlFlow::kHalt:
                break;
        }
    }
}

// The upper bound. Decodes every executable range from the front, advancing
// one byte on failure: a failed decode says nothing about where the next
// instruction starts, and skipping further would hide code behind whatever
// byte happened to be undecodable.
void SweepLinearly(const std::uint8_t* image, const std::size_t image_size,
                   const CensusOptions& options,
                   const std::vector<ExecRange>& ranges, CensusResult* result)
{
    const Decoder decoder;
    for (const ExecRange& range : ranges)
    {
        if (range.start < options.base)
        {
            continue;
        }
        std::uint32_t offset = range.start - options.base;
        const std::uint64_t range_end =
            std::min<std::uint64_t>(static_cast<std::uint64_t>(offset) +
                                        range.length,
                                    image_size);
        while (offset < range_end)
        {
            DecodedInstruction decoded;
            if (decoder.Decode(image + offset,
                               static_cast<std::size_t>(range_end - offset),
                               options.base + offset, &decoded))
            {
                ++result->sweep_decoded;
                result->sweep_mnemonics.insert(decoded.MnemonicName());
                offset += decoded.Length();
                continue;
            }
            ++result->sweep_failed;
            ++offset;
        }
    }
}

}  // namespace

CensusResult RunCensus(const std::uint8_t* image, const std::size_t image_size,
                       const CensusOptions& options)
{
    CensusResult result;
    if (image == nullptr || image_size == 0)
    {
        return result;
    }

    std::vector<ExecRange> ranges = options.exec_ranges;
    if (ranges.empty())
    {
        ranges.push_back(ExecRange{
            options.base, static_cast<std::uint32_t>(std::min<std::size_t>(
                              image_size, 0xFFFFFFFFU))});
    }

    WalkReachable(image, image_size, options, ranges, &result);
    SweepLinearly(image, image_size, options, ranges, &result);
    return result;
}

}  // namespace rex86::census
