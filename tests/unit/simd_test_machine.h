#ifndef REX86_TESTS_UNIT_SIMD_TEST_MACHINE_H_
#define REX86_TESTS_UNIT_SIMD_TEST_MACHINE_H_

#include <cstdint>
#include <initializer_list>
#include <vector>

#include "rex86/cpu.h"

// A flat 64 KiB machine for the MMX and SSE unit tests (design #29): code
// at 0x1000 ending in HLT, data at 0x4000 (EBX points there), stack at
// 0xF000.
namespace rex86::test
{

class SimdEnvironment final : public Environment
{
public:
    bool LoadDescriptor(std::uint16_t, Descriptor* descriptor) override
    {
        *descriptor = Descriptor{};
        return true;
    }
    bool PortRead(std::uint16_t, std::uint8_t, std::uint32_t*) override { return false; }
    bool PortWrite(std::uint16_t, std::uint8_t, std::uint32_t) override { return false; }
    bool InterruptTarget(std::uint8_t, std::uint16_t*, std::uint32_t*) override { return false; }
    std::uint64_t ReadTimeStampCounter() override { return 0; }
    void Cpuid(std::uint32_t, std::uint32_t, std::uint32_t r[4]) override
    {
        r[0] = r[1] = r[2] = r[3] = 0;
    }
};

struct SimdMachine
{
    static constexpr std::uint32_t kCode = 0x1000;
    static constexpr std::uint32_t kData = 0x4000;
    static constexpr std::uint32_t kStack = 0xF000;

    std::vector<std::uint8_t> buffer;
    GuestMemory memory;
    SimdEnvironment environment;
    Cpu cpu;

    SimdMachine(std::initializer_list<std::uint8_t> program, const Features& features = Features{})
        : buffer(0x10000, 0),
          memory(buffer.data(), 0x10000),
          cpu(&memory, &environment, features)
    {
        memory.pages().Set(0, 0x10000, kPageReadWriteExecute);
        std::uint32_t at = kCode;
        for (const std::uint8_t byte : program)
        {
            buffer[at++] = byte;
        }
        buffer[at] = 0xF4;
        cpu.state().eip = kCode;
        cpu.state().Set(Gpr::kEbx, kData);
        cpu.state().Set(Gpr::kEsp, kStack);
    }

    Event Run()
    {
        return cpu.Run(100);
    }

    void Put64(std::uint32_t address, std::uint64_t value)
    {
        for (unsigned i = 0; i < 8; ++i)
        {
            buffer[address + i] = static_cast<std::uint8_t>(value >> (8 * i));
        }
    }

    std::uint64_t Get64(std::uint32_t address) const
    {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < 8; ++i)
        {
            value |= static_cast<std::uint64_t>(buffer[address + i]) << (8 * i);
        }
        return value;
    }

    std::uint32_t Get32(std::uint32_t address) const
    {
        return static_cast<std::uint32_t>(Get64(address));
    }

    void SetMm(unsigned index, std::uint64_t value)
    {
        for (unsigned i = 0; i < 8; ++i)
        {
            cpu.state().x87.registers[index][i] = static_cast<std::uint8_t>(value >> (8 * i));
        }
    }

    std::uint64_t Mm(unsigned index) const
    {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < 8; ++i)
        {
            value |= static_cast<std::uint64_t>(cpu.state().x87.registers[index][i]) << (8 * i);
        }
        return value;
    }

    void SetXmm(unsigned index, std::uint64_t low, std::uint64_t high)
    {
        for (unsigned i = 0; i < 8; ++i)
        {
            cpu.state().sse.xmm[index][i] = static_cast<std::uint8_t>(low >> (8 * i));
            cpu.state().sse.xmm[index][8 + i] = static_cast<std::uint8_t>(high >> (8 * i));
        }
    }

    std::uint64_t XmmHalf(unsigned index, unsigned half) const
    {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < 8; ++i)
        {
            value |= static_cast<std::uint64_t>(cpu.state().sse.xmm[index][8 * half + i]) << (8 * i);
        }
        return value;
    }
};

}  // namespace rex86::test

#endif  // REX86_TESTS_UNIT_SIMD_TEST_MACHINE_H_
