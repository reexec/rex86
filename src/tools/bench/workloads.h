// The synthetic workloads (design #27, decision 2): five kernels, each a
// subroutine standing for one shape of game code, and six drivers that
// call them and end at a gate address. Every workload has a C++ reference
// model so the harness never credits an engine that is fast and wrong.

#ifndef REX86_TOOLS_BENCH_WORKLOADS_H_
#define REX86_TOOLS_BENCH_WORKLOADS_H_

#include <cstdint>
#include <string>
#include <vector>

#include "rex86/guest_memory.h"

namespace rex86::bench
{

// The guest memory layout: 4 MiB, code read-execute, data and stack
// read-write, everything else unmapped.
inline constexpr std::uint32_t kMemoryBytes = 0x00400000u;
inline constexpr std::uint32_t kCodeBase = 0x00010000u;
inline constexpr std::uint32_t kCodeSize = 0x00010000u;
inline constexpr std::uint32_t kDataBase = 0x00100000u;
inline constexpr std::uint32_t kDataSize = 0x00100000u;
inline constexpr std::uint32_t kStackBase = 0x003F0000u;
inline constexpr std::uint32_t kStackSize = 0x00010000u;
inline constexpr std::uint32_t kStackTop = 0x003FF000u;

// Where each kernel keeps its working set, inside the data region.
inline constexpr std::uint32_t kAccumulator = kDataBase;
inline constexpr std::uint32_t kMemoryArray = kDataBase + 0x10000u;
inline constexpr std::uint32_t kMemoryArrayDwords = 4096u;
inline constexpr std::uint32_t kMemoryTable = kDataBase + 0x20000u;
inline constexpr std::uint32_t kStringSource = kDataBase + 0x30000u;
inline constexpr std::uint32_t kStringDestination = kDataBase + 0x40000u;
inline constexpr std::uint32_t kStringBlockDwords = 64u;
inline constexpr std::uint32_t kX87Table = kDataBase + 0x50000u;
inline constexpr std::uint32_t kX87TableCount = 1024u;
inline constexpr std::uint32_t kX87Two = kDataBase + 0x60000u;
inline constexpr std::uint32_t kX87One = kDataBase + 0x60008u;
inline constexpr std::uint32_t kX87Temp = kDataBase + 0x60010u;
inline constexpr std::uint32_t kX87Temp2 = kDataBase + 0x60018u;
inline constexpr std::uint32_t kX87Result = kDataBase + 0x60020u;

// Iteration counts; a lap is tens of thousands of instructions.
inline constexpr std::uint32_t kAluIterations = 4096u;
inline constexpr std::uint32_t kCallIterations = 2048u;
inline constexpr std::uint32_t kStringIterations = 4096u;

enum class Kernel : std::uint8_t
{
    kAlu,
    kMemory,
    kCall,
    kString,
    kX87,
};

inline constexpr unsigned kKernelCount = 5;

struct Workload
{
    std::string name;
    std::vector<Kernel> kernels;
    // The driver's first instruction and the gate after its last.
    std::uint32_t entry = 0;
    std::uint32_t gate = 0;
    // What EDI holds at the gate when the core is right.
    std::uint32_t expected_edi = 0;
};

// One code image holding the five kernels and the six drivers.
struct Image
{
    std::uint32_t base = kCodeBase;
    std::vector<std::uint8_t> code;
    std::vector<Workload> workloads;
    std::uint32_t kernel_entry[kKernelCount] = {};
};

// Assembles the image. False with a message when the assembler fails.
bool BuildImage(Image* image, std::string* error);

// The reference model of one kernel: what EAX holds at its RET.
std::uint32_t KernelModel(Kernel kernel);
const char* KernelName(Kernel kernel);

// Lays the image and the initial data (the x87 table and constants) into
// guest memory and sets the page attributes. Memory must span kMemoryBytes.
bool LoadImage(const Image& image, GuestMemory* memory);

// The workload named, or null.
const Workload* FindWorkload(const Image& image, const std::string& name);

}  // namespace rex86::bench

#endif  // REX86_TOOLS_BENCH_WORKLOADS_H_
