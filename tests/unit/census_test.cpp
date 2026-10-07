#include "tools/census/census.h"

#include <cstdint>
#include <vector>

#include "test_support.h"

namespace
{

using rex86::census::CensusOptions;
using rex86::census::CensusResult;
using rex86::census::ExecRange;
using rex86::census::RunCensus;

}  // namespace

void RunCensusTests(rex86::test::Context& context)
{
    // A synthetic fragment at base 0x1000. The bytes after the final ret are
    // data the walk must not reach but the sweep must still look at.
    //
    //   0x1000  B8 05 00 00 00   mov eax, 5
    //   0x1005  75 03            jnz 0x100A
    //   0x1007  FF D0            call eax     (indirect; falls through)
    //   0x1009  C3               ret
    //   0x100A  CD 21            int 21h      (serviced; falls through)
    //   0x100C  DB 28            fld tbyte [eax]
    //   0x100E  EB F9            jmp 0x1009
    //   0x1010  66               data: a lone prefix the sweep cannot decode
    const std::vector<std::uint8_t> image = {
        0xB8, 0x05, 0x00, 0x00, 0x00,
        0x75, 0x03,
        0xFF, 0xD0,
        0xC3,
        0xCD, 0x21,
        0xDB, 0x28,
        0xEB, 0xF9,
        0x66,
    };

    CensusOptions options;
    options.base = 0x1000;
    options.entries.push_back(0x1000);

    const CensusResult result = RunCensus(image.data(), image.size(), options);

    // Reached: mov, jnz, call, ret (twice visited once), int, fld, jmp.
    REX86_CHECK_EQ(context, result.reached_instructions, 7U);
    REX86_CHECK_EQ(context, result.decode_stops, 0U);
    REX86_CHECK_EQ(context, result.out_of_range_edges, 0U);
    REX86_CHECK_EQ(context, result.mnemonics.at("mov").instructions, 1U);
    REX86_CHECK_EQ(context, result.mnemonics.at("jnz").instructions, 1U);
    REX86_CHECK_EQ(context, result.mnemonics.at("call").instructions, 1U);
    REX86_CHECK_EQ(context, result.mnemonics.at("ret").instructions, 1U);
    REX86_CHECK_EQ(context, result.mnemonics.at("int").instructions, 1U);
    REX86_CHECK_EQ(context, result.mnemonics.at("fld").instructions, 1U);
    REX86_CHECK_EQ(context, result.mnemonics.at("jmp").instructions, 1U);
    REX86_CHECK_EQ(context, result.form_counts.at("mov r32,i32"), 1U);
    REX86_CHECK_EQ(context, result.form_counts.at("fld m80"), 1U);

    // The x87 tally sees the 80-bit memory operand.
    REX86_CHECK_EQ(context, result.x87.total, 1U);
    REX86_CHECK_EQ(context, result.x87.float80_memory_operands, 1U);
    REX86_CHECK_EQ(context, result.x87.control_word_access, 0U);

    // The sweep decodes the data byte's failure as one failed position.
    REX86_CHECK(context, result.sweep_decoded >= result.reached_instructions);
    REX86_CHECK_EQ(context, result.sweep_failed, 1U);

    // A walk from a second entry does not double-count visited addresses.
    CensusOptions two_entries = options;
    two_entries.entries.push_back(0x1009);
    const CensusResult again =
        RunCensus(image.data(), image.size(), two_entries);
    REX86_CHECK_EQ(context, again.reached_instructions, 7U);

    // An entry outside the executable range is counted, not followed.
    CensusOptions ranged = options;
    ranged.exec_ranges.push_back(ExecRange{0x1000, 0x0005});
    const CensusResult bounded =
        RunCensus(image.data(), image.size(), ranged);
    REX86_CHECK_EQ(context, bounded.reached_instructions, 1U);
    REX86_CHECK_EQ(context, bounded.out_of_range_edges, 1U);

    // A direct branch out of the image stops as an out-of-range edge.
    //   0x2000  EB 7E   jmp 0x2080 (outside the 2-byte image)
    const std::vector<std::uint8_t> tiny = {0xEB, 0x7E};
    CensusOptions tiny_options;
    tiny_options.base = 0x2000;
    tiny_options.entries.push_back(0x2000);
    const CensusResult escaped =
        RunCensus(tiny.data(), tiny.size(), tiny_options);
    REX86_CHECK_EQ(context, escaped.reached_instructions, 1U);
    REX86_CHECK_EQ(context, escaped.out_of_range_edges, 1U);

    // An empty image yields an empty result rather than a crash.
    const CensusResult empty = RunCensus(nullptr, 0, options);
    REX86_CHECK_EQ(context, empty.reached_instructions, 0U);
    REX86_CHECK_EQ(context, empty.sweep_decoded, 0U);
}
