#include "trace/trace_format.h"

#include <cstring>
#include <sstream>

namespace rex86::trace
{
namespace
{

constexpr char kMagic[8] = {'R', 'X', '8', '6', 'T', 'R', 'C', '1'};
constexpr std::size_t kHeaderSize = 16;

class Out
{
public:
    explicit Out(std::vector<std::uint8_t>* bytes) : bytes_(bytes)
    {
    }

    void U8(const std::uint8_t value)
    {
        bytes_->push_back(value);
    }

    void U16(const std::uint16_t value)
    {
        U8(static_cast<std::uint8_t>(value));
        U8(static_cast<std::uint8_t>(value >> 8));
    }

    void U32(const std::uint32_t value)
    {
        U16(static_cast<std::uint16_t>(value));
        U16(static_cast<std::uint16_t>(value >> 16));
    }

    void Raw(const std::vector<std::uint8_t>& data)
    {
        bytes_->insert(bytes_->end(), data.begin(), data.end());
    }

    void Registers(const trace::Registers& r)
    {
        for (const std::uint32_t value : r.gpr)
        {
            U32(value);
        }
        U32(r.eip);
        U32(r.eflags);
    }

    void ByteRuns(const std::vector<Bytes>& runs)
    {
        U16(static_cast<std::uint16_t>(runs.size()));
        for (const Bytes& run : runs)
        {
            U32(run.address);
            U16(static_cast<std::uint16_t>(run.bytes.size()));
            Raw(run.bytes);
        }
    }

private:
    std::vector<std::uint8_t>* bytes_;
};

class In
{
public:
    In(const std::uint8_t* data, const std::size_t size)
        : data_(data), size_(size)
    {
    }

    bool ok() const
    {
        return ok_;
    }

    std::size_t position() const
    {
        return position_;
    }

    std::uint8_t U8()
    {
        if (position_ + 1 > size_)
        {
            ok_ = false;
            return 0;
        }
        return data_[position_++];
    }

    std::uint16_t U16()
    {
        const std::uint16_t low = U8();
        const std::uint16_t high = U8();
        return static_cast<std::uint16_t>(low | (high << 8));
    }

    std::uint32_t U32()
    {
        const std::uint32_t low = U16();
        const std::uint32_t high = U16();
        return low | (high << 16);
    }

    void Raw(const std::size_t count, std::vector<std::uint8_t>* out)
    {
        if (position_ + count > size_)
        {
            ok_ = false;
            return;
        }
        out->assign(data_ + position_, data_ + position_ + count);
        position_ += count;
    }

    void Registers(trace::Registers* r)
    {
        for (std::uint32_t& value : r->gpr)
        {
            value = U32();
        }
        r->eip = U32();
        r->eflags = U32();
    }

    void ByteRuns(std::vector<Bytes>* runs)
    {
        const std::uint16_t count = U16();
        runs->clear();
        for (std::uint16_t i = 0; i < count && ok_; ++i)
        {
            Bytes run;
            run.address = U32();
            const std::uint16_t length = U16();
            Raw(length, &run.bytes);
            runs->push_back(std::move(run));
        }
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t position_ = 0;
    bool ok_ = true;
};

}  // namespace

void FillRegion(const std::uint32_t fill_seed, const std::uint32_t base,
                std::uint8_t* out, const std::size_t size)
{
    std::uint32_t state = fill_seed ^ base;
    if (state == 0)
    {
        state = 0x9E3779B9u;
    }
    for (std::size_t i = 0; i < size; i += 4)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        for (std::size_t b = 0; b < 4 && i + b < size; ++b)
        {
            out[i + b] = static_cast<std::uint8_t>(state >> (8 * b));
        }
    }
}

void EncodeCase(const Case& value, std::vector<std::uint8_t>* out)
{
    std::vector<std::uint8_t> body;
    Out o(&body);
    o.U8(static_cast<std::uint8_t>(value.kind));
    o.U8(static_cast<std::uint8_t>(value.mode));
    o.U16(value.flags);
    o.U32(value.features);

    o.U8(static_cast<std::uint8_t>(value.regions.size()));
    for (const Region& region : value.regions)
    {
        o.U32(region.base);
        o.U32(region.size);
        o.U8(region.page_flags);
    }
    o.U32(value.fill_seed);
    o.ByteRuns(value.patches);
    for (const std::uint16_t selector : value.selectors)
    {
        o.U16(selector);
    }
    o.U8(value.segment_present);
    o.Registers(value.input);
    o.U32(value.budget);

    o.U8(value.reason);
    o.U8(value.fault_kind);
    o.U32(value.fault_address);
    o.Registers(value.expected);
    o.U32(value.eflags_mask);
    o.U8(value.gpr_mask);
    o.ByteRuns(value.diffs);
    o.U16(static_cast<std::uint16_t>(value.ignores.size()));
    for (const Range& range : value.ignores)
    {
        o.U32(range.address);
        o.U16(range.length);
    }

    Out(out).U32(static_cast<std::uint32_t>(body.size() + 4));
    out->insert(out->end(), body.begin(), body.end());
}

bool DecodeCase(const std::uint8_t* data, const std::size_t size, Case* value,
                std::size_t* consumed, std::string* error)
{
    In in(data, size);
    const std::uint32_t case_size = in.U32();
    if (!in.ok() || case_size < 4 || case_size > size)
    {
        *error = "truncated case header";
        return false;
    }
    In body(data + 4, case_size - 4);
    Case c;
    c.kind = static_cast<CaseKind>(body.U8());
    c.mode = static_cast<RunMode>(body.U8());
    c.flags = body.U16();
    c.features = body.U32();
    const std::uint8_t region_count = body.U8();
    for (std::uint8_t i = 0; i < region_count && body.ok(); ++i)
    {
        Region region;
        region.base = body.U32();
        region.size = body.U32();
        region.page_flags = body.U8();
        c.regions.push_back(region);
    }
    c.fill_seed = body.U32();
    body.ByteRuns(&c.patches);
    for (std::uint16_t& selector : c.selectors)
    {
        selector = body.U16();
    }
    c.segment_present = body.U8();
    body.Registers(&c.input);
    c.budget = body.U32();
    c.reason = body.U8();
    c.fault_kind = body.U8();
    c.fault_address = body.U32();
    body.Registers(&c.expected);
    c.eflags_mask = body.U32();
    c.gpr_mask = body.U8();
    body.ByteRuns(&c.diffs);
    const std::uint16_t ignore_count = body.U16();
    for (std::uint16_t i = 0; i < ignore_count && body.ok(); ++i)
    {
        Range range;
        range.address = body.U32();
        range.length = body.U16();
        c.ignores.push_back(range);
    }
    if (!body.ok() || body.position() != case_size - 4)
    {
        *error = "malformed case body";
        return false;
    }
    *value = std::move(c);
    *consumed = case_size;
    return true;
}

Writer::~Writer()
{
    Close();
}

bool Writer::Open(const std::string& path, std::string* error)
{
    Close();
    file_ = std::fopen(path.c_str(), "wb");
    if (file_ == nullptr)
    {
        *error = "cannot open " + path + " for writing";
        return false;
    }
    count_ = 0;
    std::vector<std::uint8_t> header(kMagic, kMagic + sizeof kMagic);
    Out o(&header);
    o.U32(kFormatVersion);
    o.U32(0);
    return std::fwrite(header.data(), 1, header.size(), file_) == header.size();
}

bool Writer::Add(const Case& value)
{
    if (file_ == nullptr)
    {
        return false;
    }
    std::vector<std::uint8_t> bytes;
    EncodeCase(value, &bytes);
    ++count_;
    return std::fwrite(bytes.data(), 1, bytes.size(), file_) == bytes.size();
}

bool Writer::Close()
{
    if (file_ == nullptr)
    {
        return true;
    }
    std::vector<std::uint8_t> count;
    Out(&count).U32(count_);
    bool ok = std::fseek(file_, 12, SEEK_SET) == 0 &&
              std::fwrite(count.data(), 1, count.size(), file_) == count.size();
    ok = std::fclose(file_) == 0 && ok;
    file_ = nullptr;
    return ok;
}

bool ReadFile(const std::string& path, std::vector<Case>* cases,
              std::string* error)
{
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
    {
        *error = "cannot open " + path;
        return false;
    }
    std::vector<std::uint8_t> data;
    std::uint8_t chunk[65536];
    std::size_t got = 0;
    while ((got = std::fread(chunk, 1, sizeof chunk, file)) > 0)
    {
        data.insert(data.end(), chunk, chunk + got);
    }
    std::fclose(file);
    if (data.size() < kHeaderSize || std::memcmp(data.data(), kMagic, sizeof kMagic) != 0)
    {
        *error = path + " is not a rex86 trace";
        return false;
    }
    In header(data.data() + 8, 8);
    const std::uint32_t version = header.U32();
    const std::uint32_t count = header.U32();
    if (version != kFormatVersion)
    {
        *error = path + ": unsupported trace version " + std::to_string(version);
        return false;
    }
    cases->clear();
    std::size_t position = kHeaderSize;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        Case c;
        std::size_t consumed = 0;
        if (!DecodeCase(data.data() + position, data.size() - position, &c,
                        &consumed, error))
        {
            *error = path + ": case " + std::to_string(i) + ": " + *error;
            return false;
        }
        cases->push_back(std::move(c));
        position += consumed;
    }
    if (position != data.size())
    {
        *error = path + ": trailing bytes after the last case";
        return false;
    }
    return true;
}

std::string Describe(const Case& value)
{
    std::ostringstream text;
    char line[160];
    const auto registers = [&](const char* label, const Registers& r) {
        std::snprintf(line, sizeof line,
                      "%s eax=%08X ecx=%08X edx=%08X ebx=%08X esp=%08X ebp=%08X esi=%08X edi=%08X eip=%08X eflags=%08X\n",
                      label, r.gpr[0], r.gpr[1], r.gpr[2], r.gpr[3], r.gpr[4],
                      r.gpr[5], r.gpr[6], r.gpr[7], r.eip, r.eflags);
        text << line;
    };
    const auto runs = [&](const char* label, const std::vector<Bytes>& list) {
        for (const Bytes& run : list)
        {
            std::snprintf(line, sizeof line, "%s %08X:", label, run.address);
            text << line;
            for (const std::uint8_t b : run.bytes)
            {
                std::snprintf(line, sizeof line, " %02X", b);
                text << line;
            }
            text << '\n';
        }
    };
    std::snprintf(line, sizeof line, "kind=%u mode=%u flags=%04X features=%08X budget=%u fill_seed=%08X\n",
                  static_cast<unsigned>(value.kind), static_cast<unsigned>(value.mode),
                  value.flags, value.features, value.budget, value.fill_seed);
    text << line;
    for (const Region& region : value.regions)
    {
        std::snprintf(line, sizeof line, "region %08X+%X pages=%02X\n", region.base,
                      region.size, region.page_flags);
        text << line;
    }
    std::snprintf(line, sizeof line, "selectors es=%04X cs=%04X ss=%04X ds=%04X fs=%04X gs=%04X present=%02X\n",
                  value.selectors[0], value.selectors[1], value.selectors[2],
                  value.selectors[3], value.selectors[4], value.selectors[5],
                  value.segment_present);
    text << line;
    runs("patch", value.patches);
    registers("in ", value.input);
    std::snprintf(line, sizeof line, "expect reason=%u fault=%u fault_address=%08X eflags_mask=%08X gpr_mask=%02X\n",
                  value.reason, value.fault_kind, value.fault_address,
                  value.eflags_mask, value.gpr_mask);
    text << line;
    registers("out", value.expected);
    runs("diff", value.diffs);
    for (const Range& range : value.ignores)
    {
        std::snprintf(line, sizeof line, "ignore %08X+%u\n", range.address, range.length);
        text << line;
    }
    return text.str();
}

}  // namespace rex86::trace
