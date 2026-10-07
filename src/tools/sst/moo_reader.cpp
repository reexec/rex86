#include "tools/sst/moo_reader.h"

namespace rex86::sst
{

namespace
{

// A bounds-checked cursor over the file image. Every reader returns false
// on truncation instead of reading past the end.
struct Cursor
{
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t offset = 0;

    std::size_t Remaining() const
    {
        return size - offset;
    }

    bool ReadU8(std::uint8_t* value)
    {
        if (Remaining() < 1)
        {
            return false;
        }
        *value = data[offset++];
        return true;
    }

    bool ReadU16(std::uint16_t* value)
    {
        if (Remaining() < 2)
        {
            return false;
        }
        *value = static_cast<std::uint16_t>(
            data[offset] | (static_cast<std::uint16_t>(data[offset + 1]) << 8));
        offset += 2;
        return true;
    }

    bool ReadU32(std::uint32_t* value)
    {
        if (Remaining() < 4)
        {
            return false;
        }
        *value = data[offset] |
            (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(data[offset + 3]) << 24);
        offset += 4;
        return true;
    }

    bool ReadId(std::string* id)
    {
        if (Remaining() < 4)
        {
            return false;
        }
        id->assign(reinterpret_cast<const char*>(data + offset), 4);
        offset += 4;
        return true;
    }

    bool Skip(const std::size_t count)
    {
        if (Remaining() < count)
        {
            return false;
        }
        offset += count;
        return true;
    }
};

std::string TrimPadding(const std::string& text)
{
    const std::size_t end = text.find_last_not_of(' ');
    return end == std::string::npos ? std::string() : text.substr(0, end + 1);
}

bool Fail(std::string* error, const char* message)
{
    if (error != nullptr)
    {
        *error = message;
    }
    return false;
}

// RG32 and RM32 share one layout: a uint32 bitmask and one uint32 per set
// bit, in the spec's register order.
bool ParseRegisterSet32(Cursor* cursor, const std::size_t payload_size,
                        RegisterSet32* set)
{
    Cursor local{cursor->data, cursor->offset + payload_size, cursor->offset};
    if (!local.ReadU32(&set->present))
    {
        return false;
    }
    for (std::size_t index = 0; index < RegisterSet32::kCount; ++index)
    {
        if (!set->Has(index))
        {
            continue;
        }
        if (!local.ReadU32(&set->values[index]))
        {
            return false;
        }
    }
    return cursor->Skip(payload_size);
}

bool ParseRam(Cursor* cursor, const std::size_t payload_size,
              std::vector<RamEntry>* ram)
{
    Cursor local{cursor->data, cursor->offset + payload_size, cursor->offset};
    std::uint32_t count = 0;
    if (!local.ReadU32(&count))
    {
        return false;
    }
    ram->clear();
    ram->reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        RamEntry entry;
        if (!local.ReadU32(&entry.address) || !local.ReadU8(&entry.value))
        {
            return false;
        }
        ram->push_back(entry);
    }
    return cursor->Skip(payload_size);
}

// INIT and FINA payloads are sequences of subchunks. REGS/RMSK (the 16-bit
// forms) do not appear in 386 files, but skipping them is free and keeps
// the reader usable for the older suites.
bool ParseState(Cursor* cursor, const std::size_t payload_size,
                CpuStateChunk* state, std::string* error)
{
    const std::size_t end = cursor->offset + payload_size;
    while (cursor->offset < end)
    {
        std::string id;
        std::uint32_t length = 0;
        if (!cursor->ReadId(&id) || !cursor->ReadU32(&length) ||
            length > end - cursor->offset)
        {
            return Fail(error, "truncated state subchunk");
        }
        if (id == "RG32")
        {
            if (!ParseRegisterSet32(cursor, length, &state->regs))
            {
                return Fail(error, "bad RG32 chunk");
            }
        }
        else if (id == "RM32")
        {
            if (!ParseRegisterSet32(cursor, length, &state->masks))
            {
                return Fail(error, "bad RM32 chunk");
            }
            state->has_masks = true;
        }
        else if (id == "RAM ")
        {
            if (!ParseRam(cursor, length, &state->ram))
            {
                return Fail(error, "bad RAM chunk");
            }
        }
        else
        {
            // QUEU, EA32, REGS, RMSK and anything newer: skip by length.
            if (!cursor->Skip(length))
            {
                return Fail(error, "truncated state subchunk payload");
            }
        }
    }
    return true;
}

bool ParseTest(Cursor* cursor, const std::size_t payload_size, MooTest* test,
               std::string* error)
{
    const std::size_t end = cursor->offset + payload_size;
    if (!cursor->ReadU32(&test->index))
    {
        return Fail(error, "truncated TEST index");
    }
    while (cursor->offset < end)
    {
        std::string id;
        std::uint32_t length = 0;
        if (!cursor->ReadId(&id) || !cursor->ReadU32(&length) ||
            length > end - cursor->offset)
        {
            return Fail(error, "truncated TEST subchunk");
        }
        const std::size_t next = cursor->offset + length;
        if (id == "NAME")
        {
            std::uint32_t name_length = 0;
            if (!cursor->ReadU32(&name_length) ||
                name_length > next - cursor->offset)
            {
                return Fail(error, "bad NAME chunk");
            }
            test->name.assign(
                reinterpret_cast<const char*>(cursor->data + cursor->offset),
                name_length);
        }
        else if (id == "BYTS")
        {
            std::uint32_t byte_count = 0;
            if (!cursor->ReadU32(&byte_count) ||
                byte_count > next - cursor->offset)
            {
                return Fail(error, "bad BYTS chunk");
            }
            test->bytes.assign(cursor->data + cursor->offset,
                               cursor->data + cursor->offset + byte_count);
        }
        else if (id == "INIT")
        {
            if (!ParseState(cursor, length, &test->initial, error))
            {
                return false;
            }
        }
        else if (id == "FINA")
        {
            if (!ParseState(cursor, length, &test->final_state, error))
            {
                return false;
            }
        }
        else if (id == "EXCP")
        {
            std::uint8_t number = 0;
            std::uint32_t flag_address = 0;
            // The spec stores the exception number and the address of the
            // flags image pushed for it.
            if (!cursor->ReadU8(&number) || !cursor->ReadU32(&flag_address))
            {
                return Fail(error, "bad EXCP chunk");
            }
            test->has_exception = true;
            test->exception.number = number;
            test->exception.flag_address = flag_address;
        }
        // CYCL, HASH, IDX and anything newer fall through to the skip.
        cursor->offset = cursor->offset > next ? cursor->offset : next;
        if (cursor->offset > end)
        {
            return Fail(error, "TEST subchunk overruns TEST payload");
        }
    }
    return true;
}

}  // namespace

bool ParseMooFile(const std::uint8_t* data, const std::size_t size,
                  MooFile* out, std::string* error)
{
    if (data == nullptr || size == 0 || out == nullptr)
    {
        return Fail(error, "empty input");
    }
    Cursor cursor{data, size, 0};

    std::string id;
    std::uint32_t length = 0;
    if (!cursor.ReadId(&id) || id != "MOO " || !cursor.ReadU32(&length))
    {
        return Fail(error, "not a MOO file");
    }
    {
        Cursor header{data, cursor.offset + length, cursor.offset};
        std::uint8_t reserved = 0;
        if (!header.ReadU8(&out->version_major) ||
            !header.ReadU8(&out->version_minor) ||
            !header.ReadU8(&reserved) || !header.ReadU8(&reserved) ||
            !header.ReadU32(&out->declared_test_count) ||
            !header.ReadId(&out->cpu_id))
        {
            return Fail(error, "truncated MOO header");
        }
        out->cpu_id = TrimPadding(out->cpu_id);
        if (!cursor.Skip(length))
        {
            return Fail(error, "truncated MOO header payload");
        }
    }

    while (cursor.Remaining() >= 8)
    {
        if (!cursor.ReadId(&id) || !cursor.ReadU32(&length) ||
            length > cursor.Remaining())
        {
            return Fail(error, "truncated top-level chunk");
        }
        if (id == "META")
        {
            Cursor meta{data, cursor.offset + length, cursor.offset};
            std::uint8_t byte = 0;
            std::string mnemonic;
            std::uint32_t test_count = 0;
            // major, minor, cpu_type precede the opcode.
            if (!meta.ReadU8(&byte) || !meta.ReadU8(&byte) ||
                !meta.ReadU8(&byte) || !meta.ReadU32(&out->opcode))
            {
                return Fail(error, "truncated META chunk");
            }
            if (meta.Remaining() < 8)
            {
                return Fail(error, "truncated META mnemonic");
            }
            mnemonic.assign(
                reinterpret_cast<const char*>(data + meta.offset), 8);
            meta.offset += 8;
            out->mnemonic = TrimPadding(mnemonic);
            // test_ct (u32), file_seed (u64 read as two u32), cpu_mode (u8).
            std::uint32_t seed_half = 0;
            if (!meta.ReadU32(&test_count) || !meta.ReadU32(&seed_half) ||
                !meta.ReadU32(&seed_half) || !meta.ReadU8(&out->cpu_mode))
            {
                return Fail(error, "truncated META tail");
            }
            if (!cursor.Skip(length))
            {
                return Fail(error, "truncated META payload");
            }
        }
        else if (id == "RM32")
        {
            if (!ParseRegisterSet32(&cursor, length, &out->file_masks))
            {
                return Fail(error, "bad top-level RM32 chunk");
            }
            out->has_file_masks = true;
        }
        else if (id == "TEST")
        {
            MooTest test;
            if (!ParseTest(&cursor, length, &test, error))
            {
                return false;
            }
            out->tests.push_back(std::move(test));
        }
        else
        {
            if (!cursor.Skip(length))
            {
                return Fail(error, "truncated unknown chunk");
            }
        }
    }
    return true;
}

}  // namespace rex86::sst
