#include "tools/bench/workloads.h"

#include <algorithm>
#include <cstring>
#include <iterator>

#include "tools/bench/asm.h"

namespace rex86::bench
{

namespace
{

constexpr std::uint32_t kFnvOffset = 0x811C9DC5u;
constexpr std::uint32_t kFnvPrime = 0x01000193u;
constexpr std::uint32_t kGolden = 0x9E3779B1u;
constexpr std::uint32_t kTableMultiplier = 0x2Du;
constexpr std::uint32_t kCallSeed = 0x12345678u;

// alu: a register-only hash loop. EAX := hash, no memory.
void EmitAlu(Assembler* a)
{
    a->MovImm(Reg::kEax, kFnvOffset);
    a->MovImm(Reg::kEcx, kAluIterations);
    a->MovImm(Reg::kEdx, 0);
    const Label loop = a->NewLabel();
    a->Bind(loop);
    a->AluRR(Alu::kXor, Reg::kEax, Reg::kEcx);
    a->ImulRRI(Reg::kEax, Reg::kEax, kFnvPrime);
    a->MovRR(Reg::kEbx, Reg::kEax);
    a->ShiftRI(Shift::kShr, Reg::kEbx, 13);
    a->AluRR(Alu::kXor, Reg::kEax, Reg::kEbx);
    a->ShiftRI(Shift::kRol, Reg::kEdx, 5);
    a->AluRR(Alu::kAdd, Reg::kEdx, Reg::kEax);
    a->Dec(Reg::kEcx);
    a->Jcc(Cond::kNz, loop, BranchWidth::kShort);
    a->AluRR(Alu::kXor, Reg::kEax, Reg::kEdx);
    a->Ret();
}

std::uint32_t AluModel()
{
    std::uint32_t eax = kFnvOffset;
    std::uint32_t edx = 0;
    for (std::uint32_t ecx = kAluIterations; ecx != 0; --ecx)
    {
        eax ^= ecx;
        eax *= kFnvPrime;
        const std::uint32_t ebx = eax >> 13;
        eax ^= ebx;
        edx = (edx << 5) | (edx >> 27);
        edx += eax;
    }
    return eax ^ edx;
}

// memory: fills a dword array and a byte table, then walks the array with
// loads, a table lookup and a store back. EAX := the running sum.
void EmitMemory(Assembler* a)
{
    a->MovImm(Reg::kEsi, kMemoryArray);
    a->MovImm(Reg::kEcx, 0);
    const Label fill = a->NewLabel();
    a->Bind(fill);
    a->MovRR(Reg::kEax, Reg::kEcx);
    a->ImulRRI(Reg::kEax, Reg::kEax, kGolden);
    a->MovStore(Mem::BaseIndex(Reg::kEsi, Reg::kEcx, 4, 0), Reg::kEax);
    a->Inc(Reg::kEcx);
    a->AluRI(Alu::kCmp, Reg::kEcx, kMemoryArrayDwords);
    a->Jcc(Cond::kB, fill, BranchWidth::kShort);

    a->MovImm(Reg::kEcx, 0);
    const Label table = a->NewLabel();
    a->Bind(table);
    a->MovRR(Reg::kEax, Reg::kEcx);
    a->ImulRRI(Reg::kEax, Reg::kEax, kTableMultiplier);
    a->MovStore8(Mem::IndexDisp(Reg::kEcx, 1, kMemoryTable), Reg::kEax);
    a->Inc(Reg::kEcx);
    a->AluRI(Alu::kCmp, Reg::kEcx, 256);
    a->Jcc(Cond::kB, table, BranchWidth::kShort);

    a->MovImm(Reg::kEax, 0);
    a->MovImm(Reg::kEcx, 0);
    const Label sum = a->NewLabel();
    a->Bind(sum);
    a->MovLoad(Reg::kEbx, Mem::BaseIndex(Reg::kEsi, Reg::kEcx, 4, 0));
    a->AluRR(Alu::kAdd, Reg::kEax, Reg::kEbx);
    a->MovRR(Reg::kEdx, Reg::kEbx);
    a->AluRI(Alu::kAnd, Reg::kEdx, 0xFF);
    a->MovzxLoad8(Reg::kEdx, Mem::IndexDisp(Reg::kEdx, 1, kMemoryTable));
    a->AluRR(Alu::kAdd, Reg::kEax, Reg::kEdx);
    a->MovStore(Mem::BaseIndex(Reg::kEsi, Reg::kEcx, 4, 0), Reg::kEax);
    a->Inc(Reg::kEcx);
    a->AluRI(Alu::kCmp, Reg::kEcx, kMemoryArrayDwords);
    a->Jcc(Cond::kB, sum, BranchWidth::kShort);
    a->Ret();
}

std::uint32_t MemoryModel()
{
    std::vector<std::uint32_t> array(kMemoryArrayDwords);
    for (std::uint32_t i = 0; i < kMemoryArrayDwords; ++i)
    {
        array[i] = i * kGolden;
    }
    std::uint8_t table[256];
    for (std::uint32_t i = 0; i < 256; ++i)
    {
        table[i] = static_cast<std::uint8_t>(i * kTableMultiplier);
    }
    std::uint32_t eax = 0;
    for (std::uint32_t i = 0; i < kMemoryArrayDwords; ++i)
    {
        const std::uint32_t ebx = array[i];
        eax += ebx;
        eax += table[ebx & 0xFFu];
        array[i] = eax;
    }
    return eax;
}

// call: a loop calling f(x) with one stack argument through a standard
// frame; f returns with RET 4. EAX := the last f.
void EmitCall(Assembler* a)
{
    const Label f = a->NewLabel();
    a->MovImm(Reg::kEsi, kCallSeed);
    a->MovImm(Reg::kEcx, kCallIterations);
    const Label loop = a->NewLabel();
    a->Bind(loop);
    a->Push(Reg::kEcx);
    a->Push(Reg::kEsi);
    a->Call(f);
    a->Pop(Reg::kEcx);
    a->MovRR(Reg::kEsi, Reg::kEax);
    a->Dec(Reg::kEcx);
    a->Jcc(Cond::kNz, loop, BranchWidth::kShort);
    a->MovRR(Reg::kEax, Reg::kEsi);
    a->Ret();

    a->Bind(f);
    a->Push(Reg::kEbp);
    a->MovRR(Reg::kEbp, Reg::kEsp);
    a->Push(Reg::kEbx);
    a->MovLoad(Reg::kEax, Mem::BaseDisp(Reg::kEbp, 8));
    a->Lea(Reg::kEbx, Mem::BaseIndex(Reg::kEax, Reg::kEax, 2, 1));
    a->MovRR(Reg::kEdx, Reg::kEax);
    a->ShiftRI(Shift::kShr, Reg::kEdx, 3);
    a->AluRR(Alu::kXor, Reg::kEax, Reg::kEbx);
    a->AluRR(Alu::kXor, Reg::kEax, Reg::kEdx);
    a->Pop(Reg::kEbx);
    a->Pop(Reg::kEbp);
    a->RetImm(4);
}

std::uint32_t CallModel()
{
    std::uint32_t x = kCallSeed;
    for (std::uint32_t i = 0; i < kCallIterations; ++i)
    {
        const std::uint32_t ebx = x * 3u + 1u;
        const std::uint32_t edx = x >> 3;
        x = x ^ ebx ^ edx;
    }
    return x;
}

// string: fills a block with REP STOSD, copies it with REP MOVSD and reads
// two dwords back. EAX := the checksum. Each REP iteration is one step of
// the budget (design #32), so this kernel's figures count iterations.
void EmitString(Assembler* a)
{
    a->Cld();
    a->MovImm(Reg::kEbx, kStringIterations);
    a->MovImm(Reg::kEdx, 0);
    const Label loop = a->NewLabel();
    a->Bind(loop);
    a->MovRR(Reg::kEax, Reg::kEbx);
    a->ImulRRI(Reg::kEax, Reg::kEax, kFnvPrime);
    a->MovImm(Reg::kEdi, kStringSource);
    a->MovImm(Reg::kEcx, kStringBlockDwords);
    a->RepStosd();
    a->MovImm(Reg::kEsi, kStringSource);
    a->MovImm(Reg::kEdi, kStringDestination);
    a->MovImm(Reg::kEcx, kStringBlockDwords);
    a->RepMovsd();
    a->AluRM(Alu::kAdd, Reg::kEdx, Mem::Abs(kStringDestination));
    a->AluRM(Alu::kAdd, Reg::kEdx,
             Mem::Abs(kStringDestination + 4 * (kStringBlockDwords - 1)));
    a->Dec(Reg::kEbx);
    a->Jcc(Cond::kNz, loop, BranchWidth::kShort);
    a->MovRR(Reg::kEax, Reg::kEdx);
    a->Ret();
}

std::uint32_t StringModel()
{
    std::uint32_t edx = 0;
    for (std::uint32_t ebx = kStringIterations; ebx != 0; --ebx)
    {
        const std::uint32_t eax = ebx * kFnvPrime;
        edx += eax;
        edx += eax;
    }
    return edx;
}

// x87: acc := sum over the table of (t[i] * 2.0 + 1.0), every value exactly
// representable, then FISTP to an integer. EAX := the integer.
void EmitX87(Assembler* a)
{
    a->Fldz();
    a->MovImm(Reg::kEcx, 0);
    const Label loop = a->NewLabel();
    a->Bind(loop);
    a->FldM64(Mem::IndexDisp(Reg::kEcx, 8, kX87Table));
    a->FmulM64(Mem::Abs(kX87Two));
    a->FaddM64(Mem::Abs(kX87One));
    a->FaddpSt1St0();
    a->Inc(Reg::kEcx);
    a->AluRI(Alu::kCmp, Reg::kEcx, kX87TableCount);
    a->Jcc(Cond::kB, loop, BranchWidth::kShort);
    // Exercise FXCH and FSTP on the way out; the stack ends empty.
    a->FldM64(Mem::Abs(kX87Two));
    a->FxchSt1();
    a->FstpM64(Mem::Abs(kX87Temp));
    a->FstpM64(Mem::Abs(kX87Temp2));
    a->FldM64(Mem::Abs(kX87Temp));
    a->FistpM32(Mem::Abs(kX87Result));
    a->MovLoad(Reg::kEax, Mem::Abs(kX87Result));
    a->Ret();
}

std::uint32_t X87Model()
{
    // sum_{i=0}^{N-1} (i * 0.5 * 2 + 1) = N (N + 1) / 2.
    return kX87TableCount * (kX87TableCount + 1u) / 2u;
}

// blocks: kBlocksCount distinct two-instruction blocks, each an ADD and a
// short jump over one INT3 byte to the next, so that every lap enters every
// block once. A jump to the very next instruction would not end the
// interpreter's block, whose loop sees no taken branch there.
// EAX := the sum of the immediates. It measures code that is wide rather
// than hot: the module count a translation backend needs (design #49).
std::uint8_t BlockImmediate(const std::uint32_t i)
{
    return static_cast<std::uint8_t>((i * 37u + 11u) & 0x7Fu);
}

void EmitBlocks(Assembler* a)
{
    a->MovImm(Reg::kEax, 0);
    for (std::uint32_t i = 0; i < kBlocksCount; ++i)
    {
        a->Byte(0x83);  // add eax, imm8
        a->Byte(0xC0);
        a->Byte(BlockImmediate(i));
        a->Byte(0xEB);  // jmp short over the INT3 to the next block
        a->Byte(0x01);
        a->Byte(0xCC);
    }
    a->Ret();
}

std::uint32_t BlocksModel()
{
    std::uint32_t eax = 0;
    for (std::uint32_t i = 0; i < kBlocksCount; ++i)
    {
        eax += BlockImmediate(i);
    }
    return eax;
}

// Fills with INT3 up to the next page boundary.
void PadToPage(Assembler* a)
{
    while ((a->Here() & (kGuestPageSize - 1u)) != 0)
    {
        a->Byte(0xCC);
    }
}

// The smc kernel's code, its stores naming the patched immediate and the
// variable by address; returns where those two land. An absolute operand is
// always a disp32, so the layout does not depend on the addresses given.
void EmitSmcCode(Assembler* a, const std::uint32_t site_address, const std::uint32_t variable_address,
                 std::uint32_t* site, std::uint32_t* variable)
{
    const Label loop = a->NewLabel();
    const Label skip = a->NewLabel();
    a->MovImm(Reg::kEax, 0);
    a->MovImm(Reg::kEcx, kSmcIterations);
    a->MovImm(Reg::kEdx, 1);
    a->MovStore(Mem::Abs(site_address), Reg::kEdx);
    a->Bind(loop);
    a->Byte(0x81);  // add eax, imm32: the patched immediate follows
    a->Byte(0xC0);
    *site = a->Here();
    a->Dword(1);
    a->MovRR(Reg::kEbx, Reg::kEax);
    a->ShiftRI(Shift::kRol, Reg::kEbx, 3);
    a->AluRR(Alu::kXor, Reg::kEax, Reg::kEbx);
    a->MovStore(Mem::Abs(variable_address), Reg::kEcx);
    a->MovRR(Reg::kEdx, Reg::kEcx);
    a->AluRI(Alu::kAnd, Reg::kEdx, 15);
    a->Jcc(Cond::kNz, skip, BranchWidth::kShort);
    a->MovStore(Mem::Abs(site_address), Reg::kEcx);
    a->Bind(skip);
    a->Dec(Reg::kEcx);
    a->Jcc(Cond::kNz, loop, BranchWidth::kShort);
    a->Ret();
    *variable = a->Here();
    a->Dword(0);
}

// smc: a hot loop on a page of its own that is also its data, as games
// that keep variables beside their code do (design #60). Every iteration
// stores the counter into a dword on that page, and every 16th rewrites the
// immediate of the loop's first ADD; each store invalidates the page's
// translations and decodes. The entry resets the immediate, so every lap
// starts alike. EAX := the hash.
void EmitSmc(Assembler* a, std::uint32_t* page)
{
    PadToPage(a);
    *page = a->Here();
    // A rehearsal at the same address finds the two addresses.
    Assembler rehearsal(a->Here());
    std::uint32_t site = 0;
    std::uint32_t variable = 0;
    EmitSmcCode(&rehearsal, 0, 0, &site, &variable);
    std::uint32_t placed_site = 0;
    std::uint32_t placed_variable = 0;
    EmitSmcCode(a, site, variable, &placed_site, &placed_variable);
    PadToPage(a);
}

std::uint32_t SmcModel()
{
    std::uint32_t eax = 0;
    std::uint32_t immediate = 1;
    for (std::uint32_t ecx = kSmcIterations; ecx != 0; --ecx)
    {
        eax += immediate;
        const std::uint32_t ebx = (eax << 3) | (eax >> 29);
        eax ^= ebx;
        if ((ecx & 15u) == 0)
        {
            immediate = ecx;
        }
    }
    return eax;
}

void EmitDriver(Assembler* a, const Image& image, Workload* workload)
{
    const Label entry = a->NewLabel();
    a->Bind(entry);
    workload->entry = a->Here();
    a->MovImm(Reg::kEax, 0);
    a->MovStore(Mem::Abs(kAccumulator), Reg::kEax);
    for (const Kernel kernel : workload->kernels)
    {
        // Kernels precede the drivers, so their addresses are known and
        // the CALL rel32 is computed directly.
        const std::uint32_t kernel_address =
            image.kernel_entry[static_cast<std::size_t>(kernel)];
        a->Byte(0xE8);
        const std::uint32_t next = a->Here() + 4;
        a->Dword(kernel_address - next);
        a->AluMR(Alu::kXor, Mem::Abs(kAccumulator), Reg::kEax);
    }
    a->MovLoad(Reg::kEdi, Mem::Abs(kAccumulator));
    workload->gate = a->Here();
    a->Jmp(entry);
}

void WriteDouble(GuestMemory* memory, const std::uint32_t address, const double value)
{
    std::uint8_t bytes[8];
    std::memcpy(bytes, &value, 8);
    memory->WriteBytes(address, bytes, 8);
}

}  // namespace

const char* KernelName(const Kernel kernel)
{
    switch (kernel)
    {
        case Kernel::kAlu:
            return "alu";
        case Kernel::kMemory:
            return "memory";
        case Kernel::kCall:
            return "call";
        case Kernel::kString:
            return "string";
        case Kernel::kX87:
            return "x87";
        case Kernel::kBlocks:
            return "blocks";
        case Kernel::kSmc:
            return "smc";
    }
    return "unknown";
}

std::uint32_t KernelModel(const Kernel kernel)
{
    switch (kernel)
    {
        case Kernel::kAlu:
            return AluModel();
        case Kernel::kMemory:
            return MemoryModel();
        case Kernel::kCall:
            return CallModel();
        case Kernel::kString:
            return StringModel();
        case Kernel::kX87:
            return X87Model();
        case Kernel::kBlocks:
            return BlocksModel();
        case Kernel::kSmc:
            return SmcModel();
    }
    return 0;
}

bool BuildImage(Image* image, std::string* error)
{
    Assembler a(image->base);
    using Emitter = void (*)(Assembler*);
    const Emitter emitters[] = {EmitAlu, EmitMemory, EmitCall, EmitString, EmitX87, EmitBlocks};
    for (unsigned i = 0; i < std::size(emitters); ++i)
    {
        image->kernel_entry[i] = a.Here();
        emitters[i](&a);
    }
    EmitSmc(&a, &image->smc_page);
    image->kernel_entry[static_cast<std::size_t>(Kernel::kSmc)] = image->smc_page;

    image->workloads.clear();
    // mixed keeps the five kernels it had before blocks (design #49), so
    // that its figures compare across versions.
    const Kernel mixed_kernels[] = {Kernel::kAlu, Kernel::kMemory, Kernel::kCall,
                                    Kernel::kString, Kernel::kX87};
    for (const Kernel kernel : mixed_kernels)
    {
        Workload workload;
        workload.name = KernelName(kernel);
        workload.kernels = {kernel};
        image->workloads.push_back(workload);
    }
    Workload mixed;
    mixed.name = "mixed";
    mixed.kernels.assign(std::begin(mixed_kernels), std::end(mixed_kernels));
    image->workloads.push_back(mixed);
    Workload blocks;
    blocks.name = KernelName(Kernel::kBlocks);
    blocks.kernels = {Kernel::kBlocks};
    image->workloads.push_back(blocks);
    Workload smc;
    smc.name = KernelName(Kernel::kSmc);
    smc.kernels = {Kernel::kSmc};
    image->workloads.push_back(smc);

    for (Workload& workload : image->workloads)
    {
        EmitDriver(&a, *image, &workload);
        std::uint32_t expected = 0;
        for (const Kernel kernel : workload.kernels)
        {
            expected ^= KernelModel(kernel);
        }
        workload.expected_edi = expected;
    }

    if (!a.Finish(error))
    {
        return false;
    }
    if (a.bytes().size() > kCodeSize)
    {
        *error = "the image does not fit the code region";
        return false;
    }
    image->code = a.bytes();
    return true;
}

bool LoadImage(const Image& image, GuestMemory* memory)
{
    if (memory->size() < kMemoryBytes)
    {
        return false;
    }
    PageAttributeTable& pages = memory->pages();
    pages.Set(0, kMemoryBytes, PageFlag::kNone);
    pages.Set(kCodeBase, kCodeSize, kPageReadExecute);
    pages.Set(kDataBase, kDataSize, kPageReadWrite);
    pages.Set(kStackBase, kStackSize, kPageReadWrite);
    // The smc kernel writes its own page (design #60).
    pages.Set(image.smc_page, kGuestPageSize, kPageReadWriteExecute);

    // The code pages are not writable to the guest, so the image is placed
    // through the host pointer, as a loader would.
    std::uint8_t* code = memory->HostPointer(
        image.base, static_cast<std::uint32_t>(image.code.size()));
    if (code == nullptr)
    {
        return false;
    }
    std::copy(image.code.begin(), image.code.end(), code);

    std::uint8_t* data = memory->HostPointer(kDataBase, kDataSize);
    std::uint8_t* stack = memory->HostPointer(kStackBase, kStackSize);
    if (data == nullptr || stack == nullptr)
    {
        return false;
    }
    std::fill(data, data + kDataSize, std::uint8_t{0});
    std::fill(stack, stack + kStackSize, std::uint8_t{0});

    for (std::uint32_t i = 0; i < kX87TableCount; ++i)
    {
        WriteDouble(memory, kX87Table + 8 * i, static_cast<double>(i) * 0.5);
    }
    WriteDouble(memory, kX87Two, 2.0);
    WriteDouble(memory, kX87One, 1.0);
    return true;
}

const Workload* FindWorkload(const Image& image, const std::string& name)
{
    for (const Workload& workload : image.workloads)
    {
        if (workload.name == name)
        {
            return &workload;
        }
    }
    return nullptr;
}

}  // namespace rex86::bench
