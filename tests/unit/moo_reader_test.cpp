#include "tools/sst/moo_reader.h"

#include <cstdint>
#include <string>
#include <vector>

#include "test_support.h"

namespace
{

using rex86::sst::MooFile;
using rex86::sst::ParseMooFile;

// Builders that synthesize MOO v1.1 bytes per the specification, so the
// parser is tested without the network or checked-in binary fixtures.
void PutU16(std::vector<std::uint8_t>* out, const std::uint16_t value)
{
    out->push_back(static_cast<std::uint8_t>(value & 0xFF));
    out->push_back(static_cast<std::uint8_t>(value >> 8));
}

void PutU32(std::vector<std::uint8_t>* out, const std::uint32_t value)
{
    out->push_back(static_cast<std::uint8_t>(value & 0xFF));
    out->push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out->push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out->push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

void PutId(std::vector<std::uint8_t>* out, const char (&id)[5])
{
    for (int index = 0; index < 4; ++index)
    {
        out->push_back(static_cast<std::uint8_t>(id[index]));
    }
}

void PutChunk(std::vector<std::uint8_t>* out, const char (&id)[5],
              const std::vector<std::uint8_t>& payload)
{
    PutId(out, id);
    PutU32(out, static_cast<std::uint32_t>(payload.size()));
    out->insert(out->end(), payload.begin(), payload.end());
}

std::vector<std::uint8_t> BuildRegisterSet(
    const std::uint32_t present,
    const std::vector<std::uint32_t>& values)
{
    std::vector<std::uint8_t> payload;
    PutU32(&payload, present);
    for (const std::uint32_t value : values)
    {
        PutU32(&payload, value);
    }
    return payload;
}

std::vector<std::uint8_t> BuildSampleFile()
{
    using rex86::sst::kRegEax;
    using rex86::sst::kRegEflags;
    using rex86::sst::kRegEip;

    std::vector<std::uint8_t> file;

    // MOO header: version 1.1, 1 test, CPU "386E".
    {
        std::vector<std::uint8_t> payload = {1, 1, 0, 0};
        PutU32(&payload, 1);
        PutId(&payload, "386E");
        PutChunk(&file, "MOO ", payload);
    }
    // META: versions+cpu_type, opcode 0x01, mnemonic "ADD", test_ct 1,
    // seed, cpu_mode 0 (real), reserved.
    {
        std::vector<std::uint8_t> payload = {1, 0, 7};
        PutU32(&payload, 0x01);
        const char mnemonic[9] = "ADD     ";
        for (int index = 0; index < 8; ++index)
        {
            payload.push_back(static_cast<std::uint8_t>(mnemonic[index]));
        }
        PutU32(&payload, 1);
        PutU32(&payload, 0x12345678);
        PutU32(&payload, 0x9ABCDEF0);
        payload.push_back(0);
        payload.insert(payload.end(), {0, 0, 0});
        PutChunk(&file, "META", payload);
    }
    // TEST with NAME, BYTS, INIT{RG32, RAM}, FINA{RG32, RM32, RAM}, EXCP,
    // HASH and one unknown chunk the parser must skip.
    {
        std::vector<std::uint8_t> test;
        PutU32(&test, 7);  // index

        {
            std::vector<std::uint8_t> payload;
            const std::string name = "add ax, bx";
            PutU32(&payload, static_cast<std::uint32_t>(name.size()));
            payload.insert(payload.end(), name.begin(), name.end());
            PutChunk(&test, "NAME", payload);
        }
        {
            std::vector<std::uint8_t> payload;
            PutU32(&payload, 2);
            payload.push_back(0x01);
            payload.push_back(0xD8);
            PutChunk(&test, "BYTS", payload);
        }
        {
            std::vector<std::uint8_t> init;
            const std::uint32_t present =
                (1U << kRegEax) | (1U << kRegEip) | (1U << kRegEflags);
            PutChunk(&init, "RG32",
                     BuildRegisterSet(present, {0x1111, 0x8000, 0x0002}));
            std::vector<std::uint8_t> ram;
            PutU32(&ram, 2);
            PutU32(&ram, 0x80000);
            ram.push_back(0x01);
            PutU32(&ram, 0x80001);
            ram.push_back(0xD8);
            PutChunk(&init, "RAM ", ram);
            PutChunk(&test, "INIT", init);
        }
        {
            std::vector<std::uint8_t> fina;
            PutChunk(&fina, "RG32",
                     BuildRegisterSet(1U << kRegEax, {0x2222}));
            PutChunk(&fina, "RM32",
                     BuildRegisterSet(1U << kRegEflags, {0x0800}));
            std::vector<std::uint8_t> ram;
            PutU32(&ram, 0);
            PutChunk(&fina, "RAM ", ram);
            PutChunk(&test, "FINA", fina);
        }
        {
            std::vector<std::uint8_t> payload;
            payload.push_back(0x0D);
            PutU32(&payload, 0x7FF00);
            PutChunk(&test, "EXCP", payload);
        }
        {
            std::vector<std::uint8_t> payload(20, 0xAB);
            PutChunk(&test, "HASH", payload);
        }
        {
            // A future chunk type: must be skipped by its length.
            std::vector<std::uint8_t> payload = {1, 2, 3};
            PutChunk(&test, "ZZZZ", payload);
        }
        PutChunk(&file, "TEST", test);
    }
    return file;
}

}  // namespace

void RunMooReaderTests(rex86::test::Context& context)
{
    const std::vector<std::uint8_t> sample = BuildSampleFile();

    MooFile file;
    std::string error;
    REX86_CHECK(context,
                ParseMooFile(sample.data(), sample.size(), &file, &error));
    REX86_CHECK_EQ(context, file.version_major, 1);
    REX86_CHECK_EQ(context, file.version_minor, 1);
    REX86_CHECK_EQ(context, file.declared_test_count, 1U);
    REX86_CHECK_EQ(context, file.cpu_id, std::string("386E"));
    REX86_CHECK_EQ(context, file.mnemonic, std::string("ADD"));
    REX86_CHECK_EQ(context, file.opcode, 0x01U);
    REX86_CHECK_EQ(context, file.cpu_mode, 0);
    REX86_CHECK_EQ(context, file.tests.size(), 1U);

    const rex86::sst::MooTest& test = file.tests[0];
    REX86_CHECK_EQ(context, test.index, 7U);
    REX86_CHECK_EQ(context, test.name, std::string("add ax, bx"));
    REX86_CHECK_EQ(context, test.bytes.size(), 2U);
    REX86_CHECK_EQ(context, test.bytes[0], 0x01);
    REX86_CHECK_EQ(context, test.bytes[1], 0xD8);

    REX86_CHECK(context, test.initial.regs.Has(rex86::sst::kRegEax));
    REX86_CHECK_EQ(context, test.initial.regs.values[rex86::sst::kRegEax],
                   0x1111U);
    REX86_CHECK_EQ(context, test.initial.regs.values[rex86::sst::kRegEip],
                   0x8000U);
    REX86_CHECK_EQ(context, test.initial.ram.size(), 2U);
    REX86_CHECK_EQ(context, test.initial.ram[0].address, 0x80000U);
    REX86_CHECK_EQ(context, test.initial.ram[1].value, 0xD8);

    // FINA holds only the changed registers, plus the undefined mask.
    REX86_CHECK(context, test.final_state.regs.Has(rex86::sst::kRegEax));
    REX86_CHECK(context, !test.final_state.regs.Has(rex86::sst::kRegEip));
    REX86_CHECK_EQ(context, test.final_state.regs.values[rex86::sst::kRegEax],
                   0x2222U);
    REX86_CHECK(context, test.final_state.has_masks);
    REX86_CHECK(context, test.final_state.masks.Has(rex86::sst::kRegEflags));
    REX86_CHECK_EQ(context,
                   test.final_state.masks.values[rex86::sst::kRegEflags],
                   0x0800U);

    REX86_CHECK(context, test.has_exception);
    REX86_CHECK_EQ(context, test.exception.number, 0x0D);
    REX86_CHECK_EQ(context, test.exception.flag_address, 0x7FF00U);

    // Truncated input is rejected rather than read past the end.
    for (const std::size_t cut : {std::size_t{3}, sample.size() / 2,
                                  sample.size() - 1})
    {
        MooFile partial;
        REX86_CHECK(context,
                    !ParseMooFile(sample.data(), cut, &partial, &error));
    }

    // A file that is not MOO at all is rejected.
    const std::vector<std::uint8_t> junk = {'J', 'U', 'N', 'K', 0, 0, 0, 0};
    MooFile not_moo;
    REX86_CHECK(context,
                !ParseMooFile(junk.data(), junk.size(), &not_moo, &error));
    REX86_CHECK(context, !ParseMooFile(nullptr, 0, &not_moo, &error));
}
