// The interpreter's decode cache (design #34): decoded instructions kept by
// linear address so a loop is decoded once. Self-modifying code is caught
// through the page attribute table, as the architecture requires: a slot
// sets kTranslated on its pages and records their generations, and any
// store into a kTranslated page (WriteVirtual), Cpu::InvalidateCode or a
// host's own Set/Remove clears the flag and raises the generation, which
// invalidates the slot for every Cpu sharing the memory.

#ifndef REX86_INTERP_DECODE_CACHE_H_
#define REX86_INTERP_DECODE_CACHE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "decode/decoder.h"
#include "rex86/cpu_state.h"
#include "rex86/guest_memory.h"

namespace rex86::interp
{

class DecodeCache
{
public:
    static constexpr std::uint32_t kSlots = 4096;

    DecodeCache();
    ~DecodeCache();

    DecodeCache(const DecodeCache&) = delete;
    DecodeCache& operator=(const DecodeCache&) = delete;

    // The cached decode of the instruction at linear (CS:EIP = eip) in the
    // given mode, or null. A hit needs the key, both pages' generations
    // unchanged, the pages still mapped, readable, executable and translated,
    // and for a non-flat CS the whole instruction within its limit.
    const decode::DecodedInstruction* Lookup(std::uint32_t linear, std::uint32_t eip,
                                             bool default_32bit, const SegmentRegister& cs,
                                             const GuestMemory& memory) const;

    // The slot a miss decodes into; it is invalid until Commit.
    decode::DecodedInstruction* Claim(std::uint32_t linear);

    // Validates the claimed slot once its decode succeeded: sets kTranslated
    // on the instruction's pages and records their generations.
    void Commit(std::uint32_t linear, std::uint32_t eip, bool default_32bit, GuestMemory* memory);

    // Bytes held, for the resource instrumentation (goal 7).
    [[nodiscard]] std::size_t FootprintBytes() const;

private:
    struct Key
    {
        std::uint32_t linear = 0;
        std::uint32_t eip = 0;
        std::uint32_t last_page = 0;
        std::uint32_t first_generation = 0;
        std::uint32_t last_generation = 0;
        bool default_32bit = false;
        bool valid = false;
    };

    // Raw storage: a slot is constructed when claimed, never read before.
    struct Storage
    {
        alignas(decode::DecodedInstruction) unsigned char bytes[sizeof(decode::DecodedInstruction)];
    };

    static std::uint32_t Index(const std::uint32_t linear)
    {
        return (linear ^ (linear >> 12)) & (kSlots - 1u);
    }

    const decode::DecodedInstruction* Slot(std::uint32_t index) const;

    std::vector<Key> keys_;
    std::unique_ptr<Storage[]> storage_;
};

}  // namespace rex86::interp

#endif  // REX86_INTERP_DECODE_CACHE_H_
