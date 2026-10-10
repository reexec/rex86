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

    // On a 32-bit host a real buffer's address is a guest address, so the
    // identity mapping can be exercised for real: every accessor goes through
    // integer address arithmetic, never through arithmetic on the null base
    // (Clang's UBSan reported that, #37).
    if constexpr (sizeof(void*) == 4)
    {
        std::vector<std::uint8_t> host(3 * rex86::kGuestPageSize, 0);
        const std::uint32_t page =
            (static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(host.data())) +
             rex86::kGuestPageSize - 1) &
            ~(rex86::kGuestPageSize - 1);
        std::uint8_t* const at = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(page));
        GuestMemory flat(nullptr, page + rex86::kGuestPageSize);
        REX86_CHECK(context, flat.pages().Set(page, rex86::kGuestPageSize, rex86::kPageReadWrite));
        REX86_CHECK(context, flat.Write32(page + 4, 0x78563412u));
        REX86_CHECK_EQ(context, at[4], std::uint8_t{0x12});
        REX86_CHECK_EQ(context, at[7], std::uint8_t{0x78});
        REX86_CHECK(context, flat.Write16(page + 8, 0xBEEFu));
        REX86_CHECK(context, flat.Write8(page + 10, 0x5A));
        std::uint32_t word = 0;
        std::uint16_t pair = 0;
        std::uint8_t single = 0;
        REX86_CHECK(context, flat.Read32(page + 4, &word));
        REX86_CHECK_EQ(context, word, std::uint32_t{0x78563412u});
        REX86_CHECK(context, flat.Read16(page + 8, &pair));
        REX86_CHECK_EQ(context, pair, std::uint16_t{0xBEEFu});
        REX86_CHECK(context, flat.Read8(page + 10, &single));
        REX86_CHECK_EQ(context, single, std::uint8_t{0x5A});
        const std::uint8_t bytes[3] = {1, 2, 3};
        std::uint8_t back[3] = {};
        REX86_CHECK(context, flat.WriteBytes(page + 16, bytes, sizeof(bytes)));
        REX86_CHECK(context, flat.ReadBytes(page + 16, back, sizeof(back)));
        REX86_CHECK_EQ(context, back[2], std::uint8_t{3});
        REX86_CHECK(context, flat.HostPointer(page, 4) == at);
    }
}
