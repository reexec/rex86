#include "rex86/guest_memory.h"

#include <cstring>

namespace rex86
{
namespace
{

// Pages overlapping [address, address + size), as [first, last] indices, or
// false when the range is empty or leaves the 32-bit space.
bool PageRange(std::uint32_t address,
               std::uint32_t size,
               std::uint32_t page_count,
               std::uint32_t* first,
               std::uint32_t* last)
{
    if (size == 0)
    {
        return false;
    }
    const std::uint64_t end = static_cast<std::uint64_t>(address) + size;
    if (end > (std::uint64_t{1} << 32))
    {
        return false;
    }
    *first = address >> kGuestPageShift;
    *last = static_cast<std::uint32_t>((end - 1) >> kGuestPageShift);
    return *last < page_count;
}

}  // namespace

PageAttributeTable::PageAttributeTable(std::uint32_t size_bytes)
    : flags_((static_cast<std::uint64_t>(size_bytes) + kGuestPageSize - 1) / kGuestPageSize,
             PageFlag::kNone)
{
}

PageFlag PageAttributeTable::Get(std::uint32_t address) const
{
    const std::uint32_t page = address >> kGuestPageShift;
    if (page >= flags_.size())
    {
        return PageFlag::kNone;
    }
    return flags_[page];
}

bool PageAttributeTable::Set(std::uint32_t address, std::uint32_t size, PageFlag flags)
{
    std::uint32_t first = 0;
    std::uint32_t last = 0;
    if (!PageRange(address, size, page_count(), &first, &last))
    {
        return false;
    }
    for (std::uint32_t page = first; page <= last; ++page)
    {
        flags_[page] = flags;
    }
    return true;
}

bool PageAttributeTable::Add(std::uint32_t address, std::uint32_t size, PageFlag flags)
{
    std::uint32_t first = 0;
    std::uint32_t last = 0;
    if (!PageRange(address, size, page_count(), &first, &last))
    {
        return false;
    }
    for (std::uint32_t page = first; page <= last; ++page)
    {
        flags_[page] = flags_[page] | flags;
    }
    return true;
}

bool PageAttributeTable::Remove(std::uint32_t address, std::uint32_t size, PageFlag flags)
{
    std::uint32_t first = 0;
    std::uint32_t last = 0;
    if (!PageRange(address, size, page_count(), &first, &last))
    {
        return false;
    }
    const auto mask = static_cast<std::uint8_t>(~static_cast<std::uint8_t>(flags));
    for (std::uint32_t page = first; page <= last; ++page)
    {
        flags_[page] = static_cast<PageFlag>(static_cast<std::uint8_t>(flags_[page]) & mask);
    }
    return true;
}

bool PageAttributeTable::AllHave(std::uint32_t address, std::uint32_t size, PageFlag wanted) const
{
    std::uint32_t first = 0;
    std::uint32_t last = 0;
    if (!PageRange(address, size, page_count(), &first, &last))
    {
        return false;
    }
    for (std::uint32_t page = first; page <= last; ++page)
    {
        if (!Has(flags_[page], wanted))
        {
            return false;
        }
    }
    return true;
}

GuestMemory::GuestMemory(std::uint8_t* base, std::uint32_t size)
    : base_(base), size_(size), pages_(size)
{
}

bool GuestMemory::Contains(std::uint32_t address, std::uint32_t size) const
{
    if (size == 0)
    {
        return address < size_;
    }
    const std::uint64_t end = static_cast<std::uint64_t>(address) + size;
    return end <= size_;
}

bool GuestMemory::Accessible(std::uint32_t address, std::uint32_t size, PageFlag wanted) const
{
    return Contains(address, size) && pages_.AllHave(address, size, PageFlag::kMapped | wanted);
}

bool GuestMemory::Read8(std::uint32_t address, std::uint8_t* value) const
{
    if (!Accessible(address, 1, PageFlag::kRead))
    {
        return false;
    }
    *value = base_[address];
    return true;
}

bool GuestMemory::Read16(std::uint32_t address, std::uint16_t* value) const
{
    if (!Accessible(address, 2, PageFlag::kRead))
    {
        return false;
    }
    *value = static_cast<std::uint16_t>(base_[address] | (base_[address + 1] << 8));
    return true;
}

bool GuestMemory::Read32(std::uint32_t address, std::uint32_t* value) const
{
    if (!Accessible(address, 4, PageFlag::kRead))
    {
        return false;
    }
    *value = static_cast<std::uint32_t>(base_[address]) |
             (static_cast<std::uint32_t>(base_[address + 1]) << 8) |
             (static_cast<std::uint32_t>(base_[address + 2]) << 16) |
             (static_cast<std::uint32_t>(base_[address + 3]) << 24);
    return true;
}

bool GuestMemory::Write8(std::uint32_t address, std::uint8_t value)
{
    if (!Accessible(address, 1, PageFlag::kWrite))
    {
        return false;
    }
    base_[address] = value;
    return true;
}

bool GuestMemory::Write16(std::uint32_t address, std::uint16_t value)
{
    if (!Accessible(address, 2, PageFlag::kWrite))
    {
        return false;
    }
    base_[address] = static_cast<std::uint8_t>(value);
    base_[address + 1] = static_cast<std::uint8_t>(value >> 8);
    return true;
}

bool GuestMemory::Write32(std::uint32_t address, std::uint32_t value)
{
    if (!Accessible(address, 4, PageFlag::kWrite))
    {
        return false;
    }
    base_[address] = static_cast<std::uint8_t>(value);
    base_[address + 1] = static_cast<std::uint8_t>(value >> 8);
    base_[address + 2] = static_cast<std::uint8_t>(value >> 16);
    base_[address + 3] = static_cast<std::uint8_t>(value >> 24);
    return true;
}

bool GuestMemory::ReadBytes(std::uint32_t address, std::uint8_t* bytes, std::size_t size) const
{
    if (size == 0)
    {
        return true;
    }
    if (size > 0xFFFFFFFFu || !Accessible(address, static_cast<std::uint32_t>(size), PageFlag::kRead))
    {
        return false;
    }
    std::memcpy(bytes, base_ + address, size);
    return true;
}

bool GuestMemory::WriteBytes(std::uint32_t address, const std::uint8_t* bytes, std::size_t size)
{
    if (size == 0)
    {
        return true;
    }
    if (size > 0xFFFFFFFFu || !Accessible(address, static_cast<std::uint32_t>(size), PageFlag::kWrite))
    {
        return false;
    }
    std::memcpy(base_ + address, bytes, size);
    return true;
}

std::uint8_t* GuestMemory::HostPointer(std::uint32_t address, std::uint32_t size) const
{
    if (!Contains(address, size))
    {
        return nullptr;
    }
    return base_ + address;
}

}  // namespace rex86
