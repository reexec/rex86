#include "rex86/cpu_state.h"

namespace rex86
{

void CpuState::Reset()
{
    gpr.fill(0);
    eip = 0;
    eflags = kEflagsReserved1;
    for (SegmentRegister& segment : segments)
    {
        segment = SegmentRegister{};
    }
    Seg(Segment::kCs).executable = true;
    Seg(Segment::kCs).writable = false;
    x87 = X87State{};
    sse = SseState{};
}

}  // namespace rex86
