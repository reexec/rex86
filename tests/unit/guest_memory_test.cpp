#include "rex86/guest_memory.h"

#include <cstdint>
#include <vector>

#include "test_support.h"

void RunGuestMemoryTests(rex86::test::Context& context)
{
    using rex86::GuestMemory;
    using rex86::PageFlag;

    // Four pages of guest memory at a host buffer. Guest address 0 is the
    // buffer's first byte.
    std::vector<std::uint8_t> buffer(4 * rex86::kGuestPageSize, 0);
    GuestMemory memory(buffer.data(), static_cast<std::uint32_t>(buffer.size()));
    REX86_CHECK_EQ(context, memory.pages().page_count(), std::uint32_t{4});

    // Nothing is mapped yet, so every access fails and touches nothing.
    std::uint32_t value = 0;
    REX86_CHECK(context, !memory.Read32(0x1000, &value));
    REX86_CHECK(context, !memory.Write32(0x1000, 0x11223344u));
    REX86_CHECK_EQ(context, buffer[0x1000], std::uint8_t{0});

    // Map pages 1 and 2 read-write, page 3 read-execute.
    REX86_CHECK(context, memory.pages().Set(0x1000, 0x2000, rex86::kPageReadWrite));
    REX86_CHECK(context, memory.pages().Set(0x3000, 0x1000, rex86::kPageReadExecute));
    REX86_CHECK_EQ(context, memory.pages().Get(0x0000), PageFlag::kNone);
    REX86_CHECK(context, rex86::Has(memory.pages().Get(0x1FFF), PageFlag::kWrite));
    REX86_CHECK(context, !rex86::Has(memory.pages().Get(0x3000), PageFlag::kWrite));

    // Little-endian assembly, independent of the host.
    REX86_CHECK(context, memory.Write32(0x1010, 0x78563412u));
    REX86_CHECK_EQ(context, buffer[0x1010], std::uint8_t{0x12});
    REX86_CHECK_EQ(context, buffer[0x1013], std::uint8_t{0x78});
    REX86_CHECK(context, memory.Read32(0x1010, &value));
    REX86_CHECK_EQ(context, value, std::uint32_t{0x78563412u});
    std::uint16_t half = 0;
    REX86_CHECK(context, memory.Read16(0x1011, &half));
    REX86_CHECK_EQ(context, half, std::uint16_t{0x5634u});
    std::uint8_t byte = 0;
    REX86_CHECK(context, memory.Read8(0x1013, &byte));
    REX86_CHECK_EQ(context, byte, std::uint8_t{0x78});

    // An access that straddles into an unmapped or unwritable page fails
    // whole; it does not write the first bytes and fail on the rest.
    REX86_CHECK(context, !memory.Write32(0x0FFE, 0xAABBCCDDu));
    REX86_CHECK_EQ(context, buffer[0x1000], std::uint8_t{0});
    REX86_CHECK(context, !memory.Write32(0x2FFE, 0xAABBCCDDu));
    REX86_CHECK_EQ(context, buffer[0x2FFE], std::uint8_t{0});
    REX86_CHECK(context, memory.Read32(0x2FFE, &value));
    REX86_CHECK(context, !memory.Write8(0x3000, 1));

    // Ranges that leave the buffer or wrap the 32-bit space.
    REX86_CHECK(context, !memory.Read32(0x3FFE, &value));
    REX86_CHECK(context, !memory.Contains(0xFFFFFFFCu, 8));
    REX86_CHECK(context, !memory.pages().Set(0x4000, 0x1000, rex86::kPageReadWrite));
    REX86_CHECK(context, !memory.pages().Set(0xFFFFF000u, 0x2000, rex86::kPageReadWrite));
    REX86_CHECK(context, memory.HostPointer(0x4000, 1) == nullptr);
    REX86_CHECK(context, memory.HostPointer(0x1000, 0x1000) == buffer.data() + 0x1000);

    // Bulk copies obey the same flags.
    const std::uint8_t pattern[6] = {1, 2, 3, 4, 5, 6};
    std::uint8_t copy[6] = {};
    REX86_CHECK(context, memory.WriteBytes(0x1FFD, pattern, sizeof pattern));
    REX86_CHECK(context, memory.ReadBytes(0x1FFD, copy, sizeof copy));
    REX86_CHECK_EQ(context, copy[5], std::uint8_t{6});
    REX86_CHECK(context, !memory.WriteBytes(0x2FFD, pattern, sizeof pattern));
    REX86_CHECK(context, memory.WriteBytes(0x1000, pattern, 0));

    // The translated flag is added and removed without disturbing the rest.
    REX86_CHECK(context, memory.pages().Add(0x3000, 0x1000, PageFlag::kTranslated));
    REX86_CHECK(context, rex86::Has(memory.pages().Get(0x3000), PageFlag::kTranslated));
    REX86_CHECK(context, rex86::Has(memory.pages().Get(0x3000), PageFlag::kExecute));
    REX86_CHECK(context, memory.pages().Remove(0x3000, 0x1000, PageFlag::kTranslated));
    REX86_CHECK(context, !rex86::Has(memory.pages().Get(0x3000), PageFlag::kTranslated));
    REX86_CHECK(context, rex86::Has(memory.pages().Get(0x3000), PageFlag::kExecute));
    REX86_CHECK(context, memory.pages().AllHave(0x1000, 0x2000, PageFlag::kWrite));
    REX86_CHECK(context, !memory.pages().AllHave(0x1000, 0x3000, PageFlag::kWrite));

    // A null base is the identity mapping: guest address == host address.
    GuestMemory identity(nullptr, 0x1000);
    REX86_CHECK(context, identity.HostPointer(0x10, 4) == reinterpret_cast<std::uint8_t*>(0x10));
}
