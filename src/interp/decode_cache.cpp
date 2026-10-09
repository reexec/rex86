#include "interp/decode_cache.h"

#include <new>
#include <type_traits>

namespace rex86::interp
{

static_assert(std::is_trivially_destructible_v<decode::DecodedInstruction>,
              "slots are reused without running destructors");

namespace
{

constexpr PageFlag kExecutable = kPageReadExecute | PageFlag::kTranslated;

}  // namespace

DecodeCache::DecodeCache() : keys_(kSlots)
{
}

DecodeCache::~DecodeCache() = default;

const decode::DecodedInstruction* DecodeCache::Slot(const std::uint32_t index) const
{
    return std::launder(reinterpret_cast<const decode::DecodedInstruction*>(storage_[index].bytes));
}

const decode::DecodedInstruction* DecodeCache::Lookup(const std::uint32_t linear, const std::uint32_t eip,
                                                      const bool default_32bit,
                                                      const SegmentRegister& cs,
                                                      const GuestMemory& memory) const
{
    const std::uint32_t index = Index(linear);
    const Key& key = keys_[index];
    if (!key.valid || key.linear != linear || key.eip != eip || key.default_32bit != default_32bit)
    {
        return nullptr;
    }
    const PageAttributeTable& pages = memory.pages();
    if (pages.Generation(linear) != key.first_generation || !Has(pages.Get(linear), kExecutable))
    {
        return nullptr;
    }
    const std::uint32_t last = key.last_page << kGuestPageShift;
    if (key.last_page != (linear >> kGuestPageShift) &&
        (pages.Generation(last) != key.last_generation || !Has(pages.Get(last), kExecutable)))
    {
        return nullptr;
    }
    const decode::DecodedInstruction* decoded = Slot(index);
    if (!cs.IsFlat())
    {
        // The byte fetch stops at the limit; a cached instruction must still
        // lie wholly within it.
        const std::uint64_t end = static_cast<std::uint64_t>(eip) + decoded->Length() - 1u;
        if (!cs.present || end > cs.limit)
        {
            return nullptr;
        }
    }
    return decoded;
}

decode::DecodedInstruction* DecodeCache::Claim(const std::uint32_t linear)
{
    if (!storage_)
    {
        storage_ = std::make_unique_for_overwrite<Storage[]>(kSlots);
    }
    const std::uint32_t index = Index(linear);
    keys_[index].valid = false;
    return new (storage_[index].bytes) decode::DecodedInstruction();
}

void DecodeCache::Commit(const std::uint32_t linear, const std::uint32_t eip, const bool default_32bit,
                         GuestMemory* memory)
{
    const std::uint32_t index = Index(linear);
    const std::uint32_t length = Slot(index)->Length();
    const std::uint64_t end = static_cast<std::uint64_t>(linear) + length;
    // An instruction wrapping the 4 GiB linear space is not cached.
    if (length == 0 || end > (std::uint64_t{1} << 32))
    {
        return;
    }
    const std::uint32_t last = static_cast<std::uint32_t>(end - 1u);
    PageAttributeTable& pages = memory->pages();
    if (!pages.Add(linear, length, PageFlag::kTranslated))
    {
        return;
    }
    Key& key = keys_[index];
    key.linear = linear;
    key.eip = eip;
    key.last_page = last >> kGuestPageShift;
    key.first_generation = pages.Generation(linear);
    key.last_generation = pages.Generation(last);
    key.default_32bit = default_32bit;
    key.valid = true;
}

std::size_t DecodeCache::FootprintBytes() const
{
    return keys_.size() * sizeof(Key) + (storage_ ? kSlots * sizeof(Storage) : 0u);
}

}  // namespace rex86::interp
