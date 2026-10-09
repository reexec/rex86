#ifndef REX86_GUEST_MEMORY_H_
#define REX86_GUEST_MEMORY_H_

#include <cstddef>
#include <cstdint>
#include <vector>

// The guest's flat 32-bit address space as the core sees it: one contiguous
// host buffer where guest address A lives at base + A, plus a page attribute
// table the core consults instead of hardware page protection.
//
// base may be null. That is the identity mapping, where a guest address is
// the host address, which is how rePIU's native arena and the wasm linear
// memory layout (guest at the bottom of linear memory) both look. The view
// never dereferences base alone, so a null base is not a null pointer in the
// usual sense; it is an offset of zero.
//
// Nothing here maps or protects memory through the OS. The host owns the
// buffer, and the attribute table is the only protection the core knows,
// which is what lets the same code run on wasm and on hosts that forbid
// changing page protection at run time.
namespace rex86
{

inline constexpr std::uint32_t kGuestPageSize = 4096u;
inline constexpr std::uint32_t kGuestPageShift = 12u;

enum class PageFlag : std::uint8_t
{
    kNone = 0,
    kMapped = 1,
    kRead = 2,
    kWrite = 4,
    kExecute = 8,
    // The page holds code an engine has translated; a store to it must
    // invalidate that translation before the store is visible.
    kTranslated = 16,
};

constexpr PageFlag operator|(PageFlag left, PageFlag right)
{
    return static_cast<PageFlag>(static_cast<std::uint8_t>(left) |
                                 static_cast<std::uint8_t>(right));
}

constexpr PageFlag operator&(PageFlag left, PageFlag right)
{
    return static_cast<PageFlag>(static_cast<std::uint8_t>(left) &
                                 static_cast<std::uint8_t>(right));
}

constexpr bool Has(PageFlag flags, PageFlag wanted)
{
    return (static_cast<std::uint8_t>(flags) & static_cast<std::uint8_t>(wanted)) ==
           static_cast<std::uint8_t>(wanted);
}

inline constexpr PageFlag kPageReadWrite = PageFlag::kMapped | PageFlag::kRead | PageFlag::kWrite;
inline constexpr PageFlag kPageReadExecute = PageFlag::kMapped | PageFlag::kRead | PageFlag::kExecute;
inline constexpr PageFlag kPageReadWriteExecute = kPageReadWrite | PageFlag::kExecute;

class PageAttributeTable
{
public:
    PageAttributeTable() = default;
    explicit PageAttributeTable(std::uint32_t size_bytes);

    [[nodiscard]] std::uint32_t page_count() const
    {
        return static_cast<std::uint32_t>(flags_.size());
    }

    // The flags of the page holding address, or kNone past the end.
    [[nodiscard]] PageFlag Get(std::uint32_t address) const;

    // Sets the flags of every page overlapping [address, address + size).
    // Returns false, changing nothing, when the range leaves the table.
    bool Set(std::uint32_t address, std::uint32_t size, PageFlag flags);

    // Adds flags to, or removes flags from, every page in the range.
    bool Add(std::uint32_t address, std::uint32_t size, PageFlag flags);
    bool Remove(std::uint32_t address, std::uint32_t size, PageFlag flags);

    // True when every page overlapping the range has all of wanted.
    [[nodiscard]] bool AllHave(std::uint32_t address, std::uint32_t size, PageFlag wanted) const;

    // The page's translation generation: it rises by one whenever kTranslated
    // goes from set to clear (a store into translated code, InvalidateCode,
    // or the host's own Set or Remove). An engine records it with what it
    // translated and trusts the translation only while it is unchanged, which
    // keeps every Cpu sharing this memory correct (design #34, decision 2).
    // Zero past the end.
    [[nodiscard]] std::uint32_t Generation(std::uint32_t address) const;

private:
    void Update(std::uint32_t page, PageFlag flags);

    std::vector<PageFlag> flags_;
    std::vector<std::uint32_t> generations_;
};

class GuestMemory
{
public:
    GuestMemory() = default;
    // size is the number of guest addresses the buffer covers, from 0.
    GuestMemory(std::uint8_t* base, std::uint32_t size);

    [[nodiscard]] std::uint8_t* base() const
    {
        return base_;
    }

    [[nodiscard]] std::uint32_t size() const
    {
        return size_;
    }

    [[nodiscard]] PageAttributeTable& pages()
    {
        return pages_;
    }

    [[nodiscard]] const PageAttributeTable& pages() const
    {
        return pages_;
    }

    // True when [address, address + size) lies inside the buffer without
    // wrapping around the 32-bit address space.
    [[nodiscard]] bool Contains(std::uint32_t address, std::uint32_t size) const;

    // Data accesses. Each checks the range and the page flags (kMapped plus
    // kRead or kWrite) and returns false without touching memory otherwise.
    // Multi-byte values are assembled byte by byte, little-endian, so the
    // host's alignment and byte-order rules never matter.
    bool Read8(std::uint32_t address, std::uint8_t* value) const;
    bool Read16(std::uint32_t address, std::uint16_t* value) const;
    bool Read32(std::uint32_t address, std::uint32_t* value) const;
    bool Write8(std::uint32_t address, std::uint8_t value);
    bool Write16(std::uint32_t address, std::uint16_t value);
    bool Write32(std::uint32_t address, std::uint32_t value);
    bool ReadBytes(std::uint32_t address, std::uint8_t* bytes, std::size_t size) const;
    bool WriteBytes(std::uint32_t address, const std::uint8_t* bytes, std::size_t size);

    // The host pointer of a guest address, or null when the range is not
    // inside the buffer. Page flags are not consulted: this is for hosts and
    // loaders placing an image, not for guest accesses.
    [[nodiscard]] std::uint8_t* HostPointer(std::uint32_t address, std::uint32_t size) const;

private:
    [[nodiscard]] bool Accessible(std::uint32_t address, std::uint32_t size, PageFlag wanted) const;

    std::uint8_t* base_ = nullptr;
    std::uint32_t size_ = 0;
    PageAttributeTable pages_;
};

}  // namespace rex86

#endif  // REX86_GUEST_MEMORY_H_
