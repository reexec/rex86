#ifndef REX86_ENVIRONMENT_H_
#define REX86_ENVIRONMENT_H_

#include <cstddef>
#include <cstdint>

#include "rex86/cpu_state.h"

// The contract between the core and its host (rePIU's engine, re2DJ's
// runner). The core runs the guest until something the host must handle,
// stops, and hands back an Event whose fields are all 32-bit guest values.
// The host edits the CpuState and resumes. No host pointer and no engine
// handle ever crosses this boundary, so the same host code serves the
// interpreter and every translation backend.
namespace rex86
{

// Why Run returned.
enum class StopReason : std::uint8_t
{
    // No execution engine is built into this core yet (task #1 ships the
    // state and the contract, not an engine). Not a fault and not success:
    // an explicit state, so that no caller mistakes an idle core for one
    // that ran.
    kNoEngine,
    // The instruction budget ran out. Nothing else happened.
    kBudgetExhausted,
    // Execution reached an address registered through Cpu::RegisterGate.
    // re2DJ's import thunks and rePIU's LINEXE gates arrive this way.
    kGate,
    // An INT n the host did not route elsewhere. rePIU's DOS and DPMI
    // services arrive this way.
    kSoftwareInterrupt,
    // An IN or OUT the Environment's port callbacks declined.
    kPortIo,
    // A fault; see FaultKind.
    kFault,
    // HLT with interrupts enabled: the guest waits for an interrupt.
    kHalted,
    // Cpu::Run was asked to stop by RequestStop.
    kStopRequested,
};

// The same set rePIU's platform FaultKind and re2DJ's NativeFaultKind name,
// so each adapter maps one to one.
enum class FaultKind : std::uint8_t
{
    kNone,
    kAccessViolation,
    kIllegalInstruction,
    kPrivilegedInstruction,
    kBreakpoint,
    kSingleStep,
    kDivide,
    kOverflow,
    kBound,
    kGeneralProtection,
    kOther,
};

struct Event
{
    StopReason reason = StopReason::kNoEngine;
    // kGate: the gate's linear address. kSoftwareInterrupt: the vector.
    std::uint32_t gate_address = 0;
    std::uint8_t vector = 0;
    // kFault.
    FaultKind fault_kind = FaultKind::kNone;
    std::uint32_t fault_address = 0;
    bool fault_on_write = false;
    bool fault_on_fetch = false;
    // kPortIo.
    std::uint16_t port = 0;
    std::uint8_t port_width = 0;
    bool port_is_write = false;
    std::uint32_t port_value = 0;
    // How many guest instructions the call retired before stopping.
    std::uint64_t instructions_retired = 0;
};

// What the host answers when the guest loads a selector into a segment
// register. The core caches it in the SegmentRegister.
struct Descriptor
{
    std::uint32_t base = 0;
    std::uint32_t limit = 0xFFFFFFFFu;
    bool present = true;
    bool executable = false;
    bool writable = true;
    bool default_32bit = true;
};

// Features the host enables. Everything is off until an engine implements
// it; an instruction of a disabled feature faults with kIllegalInstruction.
struct Features
{
    bool x87 = true;
    bool mmx = false;
    bool sse = false;
    bool sse2 = false;
    // D/B = 0 code and stack segments (16-bit default width), which rePIU's
    // DOS/4GW paths use.
    bool segments_16bit = false;
};

class Environment
{
public:
    virtual ~Environment() = default;

    // The descriptor for selector, or false when the selector is invalid
    // (the core then raises kGeneralProtection). Called for every segment
    // load, the null selector included: the host expresses "loadable but
    // faults on use" as present = false. A CS load needs present and
    // executable, an SS load present and writable, or the core raises
    // kGeneralProtection.
    virtual bool LoadDescriptor(std::uint16_t selector, Descriptor* descriptor) = 0;

    // Port I/O. Returning false hands the access to the host as a kPortIo
    // event instead of completing it here.
    virtual bool PortRead(std::uint16_t port, std::uint8_t width, std::uint32_t* value) = 0;
    virtual bool PortWrite(std::uint16_t port, std::uint8_t width, std::uint32_t value) = 0;

    // Where an accepted interrupt goes. The host owns the IDT (rePIU's DPMI
    // HLE sets vectors); the core pushes the frame and jumps. Returning
    // false leaves the interrupt pending and stops with kSoftwareInterrupt
    // carrying the vector.
    virtual bool InterruptTarget(std::uint8_t vector, std::uint16_t* cs, std::uint32_t* eip) = 0;

    // RDTSC and CPUID are host-defined values, never the host CPU's own.
    virtual std::uint64_t ReadTimeStampCounter() = 0;
    virtual void Cpuid(std::uint32_t leaf, std::uint32_t subleaf, std::uint32_t registers[4]) = 0;

    // Diagnostics only: a store reached a page marked kTranslated. The core
    // has already invalidated the translation.
    virtual void OnCodePageWritten(std::uint32_t page_address)
    {
        static_cast<void>(page_address);
    }
};

// Memory a translation backend may execute from, supplied by the host
// because making memory executable is an OS matter (mmap and mprotect on
// Linux and Android, MAP_JIT and per-thread write/execute switching on
// macOS, VirtualProtect on Windows). A host that gives the core no
// CodeCacheServices gets the interpreter alone, which is the only engine an
// iOS app may run.
class CodeCacheServices
{
public:
    virtual ~CodeCacheServices() = default;

    // Allocates bytes of memory that EndWrite can make executable. Null on
    // failure.
    virtual void* Allocate(std::size_t bytes) = 0;
    virtual void Release(void* memory, std::size_t bytes) = 0;
    // Brackets the core writing code into [memory, memory + bytes).
    // EndWrite makes the range executable and flushes the instruction cache.
    virtual void BeginWrite(void* memory, std::size_t bytes) = 0;
    virtual void EndWrite(void* memory, std::size_t bytes) = 0;
};

}  // namespace rex86

#endif  // REX86_ENVIRONMENT_H_
