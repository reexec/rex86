#include "translate/wasm/module_writer.h"

namespace rex86::translate::wasm
{

void AppendU32(Bytes* out, std::uint32_t value)
{
    do
    {
        std::uint8_t byte = value & 0x7Fu;
        value >>= 7;
        if (value != 0)
        {
            byte |= 0x80u;
        }
        out->push_back(byte);
    } while (value != 0);
}

void AppendS64(Bytes* out, std::int64_t value)
{
    while (true)
    {
        const std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7F);
        value >>= 7;  // arithmetic: the sign bit spreads
        const bool done = (value == 0 && (byte & 0x40) == 0) || (value == -1 && (byte & 0x40) != 0);
        out->push_back(done ? byte : static_cast<std::uint8_t>(byte | 0x80));
        if (done)
        {
            return;
        }
    }
}

void AppendS32(Bytes* out, const std::int32_t value)
{
    AppendS64(out, value);
}

void AppendName(Bytes* out, const std::string& name)
{
    AppendU32(out, static_cast<std::uint32_t>(name.size()));
    out->insert(out->end(), name.begin(), name.end());
}

void Code::LocalGet(const std::uint32_t index)
{
    bytes_.push_back(op::kLocalGet);
    AppendU32(&bytes_, index);
}

void Code::LocalSet(const std::uint32_t index)
{
    bytes_.push_back(op::kLocalSet);
    AppendU32(&bytes_, index);
}

void Code::I32Const(const std::uint32_t value)
{
    bytes_.push_back(op::kI32Const);
    AppendS32(&bytes_, static_cast<std::int32_t>(value));
}

void Code::I64Const(const std::int64_t value)
{
    bytes_.push_back(op::kI64Const);
    AppendS64(&bytes_, value);
}

void Code::Memory(const std::uint8_t opcode, const std::uint32_t offset)
{
    bytes_.push_back(opcode);
    AppendU32(&bytes_, 0);  // alignment exponent
    AppendU32(&bytes_, offset);
}

void Code::If()
{
    bytes_.push_back(op::kIf);
    bytes_.push_back(kBlockTypeEmpty);
}

void Code::CallIndirect(const std::uint32_t type_index)
{
    bytes_.push_back(op::kCallIndirect);
    AppendU32(&bytes_, type_index);
    AppendU32(&bytes_, 0);  // table 0, the imported one
}

namespace
{

void Section(Bytes* module, const std::uint8_t id, const Bytes& body)
{
    module->push_back(id);
    AppendU32(module, static_cast<std::uint32_t>(body.size()));
    module->insert(module->end(), body.begin(), body.end());
}

void FunctionType(Bytes* out, const std::uint32_t params)
{
    out->push_back(0x60);
    AppendU32(out, params);
    for (std::uint32_t i = 0; i < params; ++i)
    {
        out->push_back(kTypeI32);
    }
    AppendU32(out, 1);
    out->push_back(kTypeI32);
}

}  // namespace

Bytes WriteModule(const ModuleSpec& spec)
{
    Bytes module = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};

    Bytes types;
    AppendU32(&types, 2);
    FunctionType(&types, spec.block_params);
    FunctionType(&types, spec.helper_params);
    Section(&module, 1, types);

    // The core's memory and table, with no maximum, so they match whatever
    // limits the embedding gave them.
    Bytes imports;
    AppendU32(&imports, 2);
    AppendName(&imports, "env");
    AppendName(&imports, "memory");
    imports.push_back(0x02);  // memory
    imports.push_back(0x00);  // limits: minimum only
    AppendU32(&imports, 0);
    AppendName(&imports, "env");
    AppendName(&imports, "table");
    imports.push_back(0x01);  // table
    imports.push_back(kTypeFuncref);
    imports.push_back(0x00);
    AppendU32(&imports, 0);
    Section(&module, 2, imports);

    Bytes functions;
    AppendU32(&functions, static_cast<std::uint32_t>(spec.functions.size()));
    for (std::size_t i = 0; i < spec.functions.size(); ++i)
    {
        AppendU32(&functions, 0);
    }
    Section(&module, 3, functions);

    Bytes exports;
    AppendU32(&exports, static_cast<std::uint32_t>(spec.functions.size()));
    for (std::size_t i = 0; i < spec.functions.size(); ++i)
    {
        AppendName(&exports, "b" + std::to_string(i));
        exports.push_back(0x00);  // function
        AppendU32(&exports, static_cast<std::uint32_t>(i));
    }
    Section(&module, 7, exports);

    Bytes code;
    AppendU32(&code, static_cast<std::uint32_t>(spec.functions.size()));
    for (const ModuleSpec::Function& function : spec.functions)
    {
        Bytes body;
        if (function.locals == 0)
        {
            AppendU32(&body, 0);
        }
        else
        {
            AppendU32(&body, 1);
            AppendU32(&body, function.locals);
            body.push_back(kTypeI32);
        }
        body.insert(body.end(), function.code.begin(), function.code.end());
        body.push_back(op::kEnd);
        AppendU32(&code, static_cast<std::uint32_t>(body.size()));
        code.insert(code.end(), body.begin(), body.end());
    }
    Section(&module, 10, code);
    return module;
}

}  // namespace rex86::translate::wasm
