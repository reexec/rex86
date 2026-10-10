// A minimal encoder of the WebAssembly binary format (design #45): the
// sections, value types and instructions the wasm backend emits, nothing
// more. See https://webassembly.github.io/spec/core/binary/ for the format.

#ifndef REX86_TRANSLATE_WASM_MODULE_WRITER_H_
#define REX86_TRANSLATE_WASM_MODULE_WRITER_H_

#include <cstdint>
#include <string>
#include <vector>

namespace rex86::translate::wasm
{

using Bytes = std::vector<std::uint8_t>;

namespace op
{
inline constexpr std::uint8_t kIf = 0x04;
inline constexpr std::uint8_t kEnd = 0x0B;
inline constexpr std::uint8_t kReturn = 0x0F;
inline constexpr std::uint8_t kCallIndirect = 0x11;
inline constexpr std::uint8_t kSelect = 0x1B;
inline constexpr std::uint8_t kLocalGet = 0x20;
inline constexpr std::uint8_t kLocalSet = 0x21;
inline constexpr std::uint8_t kI32Load = 0x28;
inline constexpr std::uint8_t kI32Load8U = 0x2D;
inline constexpr std::uint8_t kI32Load16U = 0x2F;
inline constexpr std::uint8_t kI32Store = 0x36;
inline constexpr std::uint8_t kI32Store8 = 0x3A;
inline constexpr std::uint8_t kI32Store16 = 0x3B;
inline constexpr std::uint8_t kI32Const = 0x41;
inline constexpr std::uint8_t kI64Const = 0x42;
inline constexpr std::uint8_t kI32Eqz = 0x45;
inline constexpr std::uint8_t kI32Eq = 0x46;
inline constexpr std::uint8_t kI32Ne = 0x47;
inline constexpr std::uint8_t kI32LtS = 0x48;
inline constexpr std::uint8_t kI32LtU = 0x49;
inline constexpr std::uint8_t kI32Popcnt = 0x69;
inline constexpr std::uint8_t kI32Add = 0x6A;
inline constexpr std::uint8_t kI32Sub = 0x6B;
inline constexpr std::uint8_t kI32Mul = 0x6C;
inline constexpr std::uint8_t kI32And = 0x71;
inline constexpr std::uint8_t kI32Or = 0x72;
inline constexpr std::uint8_t kI32Xor = 0x73;
inline constexpr std::uint8_t kI32Shl = 0x74;
inline constexpr std::uint8_t kI32ShrS = 0x75;
inline constexpr std::uint8_t kI32ShrU = 0x76;
inline constexpr std::uint8_t kI64Mul = 0x7E;
inline constexpr std::uint8_t kI64ShrU = 0x88;
inline constexpr std::uint8_t kI32WrapI64 = 0xA7;
inline constexpr std::uint8_t kI64ExtendI32S = 0xAC;
inline constexpr std::uint8_t kI64ExtendI32U = 0xAD;
}  // namespace op

inline constexpr std::uint8_t kTypeI32 = 0x7F;
inline constexpr std::uint8_t kTypeFuncref = 0x70;
inline constexpr std::uint8_t kBlockTypeEmpty = 0x40;

void AppendU32(Bytes* out, std::uint32_t value);  // unsigned LEB128
void AppendS32(Bytes* out, std::int32_t value);   // signed LEB128
void AppendS64(Bytes* out, std::int64_t value);
void AppendName(Bytes* out, const std::string& name);

// A function body's instructions.
class Code
{
public:
    void Op(std::uint8_t opcode) { bytes_.push_back(opcode); }
    void LocalGet(std::uint32_t index);
    void LocalSet(std::uint32_t index);
    void I32Const(std::uint32_t value);
    void I64Const(std::int64_t value);
    // A load or store with alignment hint 0 (always valid) and a constant
    // offset.
    void Memory(std::uint8_t opcode, std::uint32_t offset);
    void If();  // with an empty block type
    void CallIndirect(std::uint32_t type_index);

    [[nodiscard]] const Bytes& bytes() const { return bytes_; }

private:
    Bytes bytes_;
};

// The one module shape the backend emits: two function types, the imported
// memory and table, functions of type 0 exported as b0, b1, ...
struct ModuleSpec
{
    // Type 0: the block function; type 1: the check helper.
    std::uint32_t block_params = 3;
    std::uint32_t helper_params = 5;
    // Each function: its i32 local count beyond the parameters, its code.
    struct Function
    {
        std::uint32_t locals = 0;
        Bytes code;
    };
    std::vector<Function> functions;
};

Bytes WriteModule(const ModuleSpec& spec);

}  // namespace rex86::translate::wasm

#endif  // REX86_TRANSLATE_WASM_MODULE_WRITER_H_
