# #35 설계 : 블록 단위 실행 / #35 design : block-level execution

이슈: [#35](https://github.com/reexec/rex86/issues/35) | 지시서: [20261009-i035](../work-orders/20261009-i035-block-execution.md) | 로그: [20261009-i035](../work-logs/20261009-i035-block-execution.md) | 근거: [인터프리터 성능](../analysis/interpreter-performance.md) 2.1절, [#34 설계](20261009-i034-interpreter-decode-cache.md)

## 측정 먼저 / Measured first

이슈는 "분기 사이의 명령을 묶어 한 번에 찾고 연달아 실행"을 제안했다. 그 근거였던 #34의 분석(`interp::Step` 자체 39%를 "상태 저장과 분기"로 읽음)을 먼저 다시 쟀다. 같은 기계(AMD Ryzen 5 5600X)다.

**도구**: perf와 valgrind는 없고(sudo 비밀번호 필요), gprof의 줄 단위 모드(`-l`)는 이 바이너리에서 "somebody miscounted"로 깨진다. 그래서 작업 중에만 쓰는 표본 프로파일러 둘을 만들었다(커밋하지 않음). Linux는 `LD_PRELOAD`로 `SIGPROF` 타이머를 걸어 RIP를 모으고 `addr2line`으로 인라인된 함수와 줄을 찾는다. Windows는 자식 프로세스를 1 ms마다 멈춰 EIP를 읽고 DbgHelp로 PDB에서 찾는다.

**GCC(x86-64, `-O3 -g`), #34 HEAD, alu·memory·call 표본 2,948개**. **확인됨**.

| 위치 | 비중 | 내용 |
|---|---|---|
| `Step`의 `std::optional<DecodedInstruction> local;` 줄 | **31.9%** | 디스어셈블하면 `rep stos`로 1,144바이트(0x8f qword)를 0으로 채운다. 캐시 적중이어서 쓰지 않는 지역 변수인데도 명령마다 채운다 |
| `std::unordered_set` 게이트 조회 | 약 6% | 벤치마크는 게이트 하나를 등록한다(소비자처럼). 게이트가 있으면 명령마다 해시 조회 |
| `RunUntilStop` 나머지 | 약 5% | 정지 요청, 인터럽트, 예산 검사 |
| `DecodeCache::Lookup` | 2.7% | |
| `FeatureEnabled`, `Flow` | 2.7%, 1.9% | |

#34 분석의 "Step 자체 39%"는 대부분 상태 저장이 아니라 이 0 채우기였다(gprof의 함수 단위 수치로는 나눌 수 없었다).

**MSVC(x86, `/O2 /Ob2`)는 다르다**: 0 채우기가 없다. 아래의 미스 경로 분리만 넣으면 오히려 3~8% 느려졌고, 블록 루프를 더하면 기준선보다 5~10% 빨라졌다. 그 상태의 표본 5,591개(alu·memory·call)에서 `SlotOf`(Zydis 레지스터 id를 레지스터 파일 칸으로 바꾸는 switch) **16.0%**, `RunBlock`의 `block.last = StepOne(...)` 줄(`Event`를 담은 `StepResult`의 복사) **8.5%**가 컸다. **확인됨**.

**효과가 없어 넣지 않은 것**(번갈아 잰 전후가 잡음 범위 안, GCC): 세그먼트 레지스터 여섯 개를 명령마다 복사하지 않고 `LoadSegment`가 처음 쓸 때만 보관하는 방식(프로파일에서 그 줄 4.3%). 되돌렸다.

**블록 단위 조회는 하지 않는다**: 고친 뒤의 GCC 프로파일에서 캐시 조회 전체(`Lookup`, 페이지 속성, 세대)는 약 11%이고, 기본 블록을 미리 묶어 조회를 줄여도 그 일부만 준다. 반면 기본 블록 캐시는 무효화(SMC, 게이트 등록), 메모리, 블록 경계 규칙을 새로 들여온다. 남은 시간은 의미 실행(Zydis 피연산자 해석, `ReadOperand`/`WriteOperand`)과 저장 경로(`WriteVirtual`, `Write32`, 문자열 명령)에 있다(분석 2.2절). 이것들은 3단계 번역 백엔드나 별도 작업의 몫이다.

*The issue proposed looking up the instructions between branches once and running them back to back. Its premise, #34's reading of `interp::Step` itself at 39% as "state saving and dispatch", was re-measured first, on the same machine (AMD Ryzen 5 5600X). With no perf or valgrind (sudo needs a password) and gprof's line mode (`-l`) failing on this binary ("somebody miscounted"), two scratch sampling profilers were built for the task and not committed: on Linux an `LD_PRELOAD` library collecting RIP on a `SIGPROF` timer, resolved by `addr2line` to inlined functions and lines; on Windows a launcher suspending the child every millisecond to read EIP, resolved through DbgHelp from the PDB. GCC (x86-64, `-O3 -g`) on #34's HEAD, 2,948 samples over alu, memory and call, confirmed: the `std::optional<DecodedInstruction> local;` line in `Step` took **31.9%**, which the disassembly shows as a `rep stos` zeroing 1,144 bytes (0x8f qwords) on every instruction, cache hits included, where the local goes unused; the `std::unordered_set` gate lookup about 6% (the benchmark registers one gate, as consumers do); the rest of `RunUntilStop` about 5%; `DecodeCache::Lookup` 2.7%; `FeatureEnabled` 2.7% and `Flow` 1.9%. #34's "Step itself 39%" was mostly this zeroing, not state saving, which gprof's per-function figure could not separate. MSVC (x86, `/O2 /Ob2`) differs: no zeroing; the miss-path split below alone made it 3-8% slower, and adding the block loop made it 5-10% faster than the baseline; in that state, 5,591 samples showed `SlotOf` (the switch mapping a Zydis register id to a register-file slot) at **16.0%** and `RunBlock`'s `block.last = StepOne(...)` line (copying a `StepResult` with its `Event`) at **8.5%**, confirmed. Left out for lack of effect (interleaved before and after within the noise, GCC): keeping segment registers only when `LoadSegment` first writes each instead of copying all six per instruction (that line 4.3% in the profile), reverted. No block-wise lookup: after the fixes the whole lookup (`Lookup`, page attributes, generations) is about 11% under GCC, and pre-linking basic blocks would remove only part of it while bringing new invalidation (SMC, gate registration), memory and block-boundary rules; the remaining time is in semantics (Zydis operand resolution, `ReadOperand`/`WriteOperand`) and the store path (`WriteVirtual`, `Write32`, string instructions) (analysis section 2.2), for phase 3's translation backends or tasks of their own.*

## 결정 1: 적중 경로에 디코드 버퍼를 두지 않는다 / Decision 1: no decode buffer on the hit path

`interp::Step`을 셋으로 나눈다.

* `ExecuteDecoded`: 디코드된 명령 하나를 실행한다(기능 검사, 상태 저장, `Execute`, 분기 목적지 검사, 폴트 복원). 인라인.
* `StepMiss`: 인출, 디코드(캐시 칸이나 지역 버퍼에), 캐시 등록, 그리고 `ExecuteDecoded`. **인라인하지 않는다**(`REX86_NOINLINE`). 1,144바이트 지역 버퍼는 이 함수에만 있다.
* `StepOne`: 캐시 적중이면 `ExecuteDecoded`, 아니면 `StepMiss`.

결과는 `StepResult&`에 제자리로 쓴다. 블록 루프가 명령마다 `StepResult`를 복사하지 않게 하기 위해서다(MSVC 8.5%). 단순 retire는 `status`와 `inhibit_interrupts`만 쓴다. `inhibit_interrupts`는 명령마다 먼저 지운다(앞 명령의 값이 폴트 경로에 남지 않게).

분기 뒤처리는 싼 검사를 먼저 한다: 다음 EIP가 바로 다음 명령이고 CS 선택자가 그대로면 `Flow()`를 부르지 않는다. 의미는 같다(논리곱의 순서만 바뀜).

*`interp::Step` splits into three: `ExecuteDecoded` runs one decoded instruction (feature check, state saving, `Execute`, the branch-target check, fault restoration), inline; `StepMiss` fetches, decodes (into the cache's slot or a local buffer), commits and calls `ExecuteDecoded`, **not inlined** (`REX86_NOINLINE`), the 1,144-byte local living there alone; `StepOne` takes the hit path to `ExecuteDecoded` and otherwise `StepMiss`. The result is written in place through `StepResult&` so the block loop does not copy one per instruction (8.5% under MSVC); a plain retirement writes only `status` and `inhibit_interrupts`, the latter cleared first for every instruction so a previous value never lingers on a fault path. The branch epilogue tests the cheap part first: when the next EIP is the fall-through and the CS selector is unchanged, `Flow()` is not called; the meaning is the same, only the order of a conjunction changes.*

## 결정 2: 블록 루프 / Decision 2: the block loop

`interp::RunBlock(state, memory, environment, features, cache, limits)`는 `StepOne`을 이어 부른다. 여기서 "블록"은 `Cpu::Run`의 검사 사이에 연달아 실행되는 명령들이다. 분기에서 끊지 않는다: 의미상 끊을 이유가 없고(분기의 목적지 검사는 명령 안에서 끝난다), 끊으면 루프 왕복만 는다. 다음 중 하나에서 돌아온다.

| 조건 | 이유 |
|---|---|
| 명령이 단순히 retire하지 않음(정지, 폴트, 미구현) | 지금처럼 `Run`이 이벤트를 돌려준다 |
| `limits.max_instructions`에 닿음 | 예산과 디코드 캐시 예열(#34)의 남은 수 |
| `*limits.attention`이 섬 | 정지 요청이나 대기 중인 인터럽트. `Run`이 처리한다 |
| 다음 명령이 게이트일 수 있음 | 게이트 필터(결정 3)가 양성이면 `Run`이 정확한 집합으로 확인한다 |

`attention`은 `Cpu`의 원자적 플래그다(아래). `RaiseInterrupt`와 `RequestStop`이 세우고, `ClearPendingInterrupt`와 정지 요청 처리 뒤 `RefreshAttention`이 다시 계산한다(대기 인터럽트가 있거나 정지 요청이 있으면 참). 명령이 호스트 콜백(포트, CPUID 등) 안에서 인터럽트를 올리거나 정지를 요청해도 다음 경계에서 `Run`으로 돌아가므로 지금과 같다. IF가 꺼진 채 인터럽트가 대기하면 `attention`이 계속 참이어서 블록은 명령 하나씩 돈다. 지금과 같은 속도이고 의미도 같다. 인터럽트 그림자(MOV SS, STI)는 블록의 마지막 명령 값을 `Run`이 받는다. 블록 안에서 그림자 다음 명령까지 실행됐다면 그림자는 이미 지나간 것이고, 그때 `attention`이 거짓이었으므로 전달할 인터럽트도 없었다.

`RequestStop`은 다른 호스트 스레드에서 불릴 수 있다고 공개 헤더가 말한다. 그래서 `stop_requested_`와 `attention_`은 `std::atomic<bool>`을 감싼 `Cpu::AtomicFlag`다. `std::atomic`은 이동할 수 없으므로 `AtomicFlag`가 값을 옮기는 이동을 정의해 `Cpu`의 이동 가능성을 지킨다(다른 스레드가 쓰는 동안 `Cpu`를 옮기는 것은 어차피 경합이다). 처음 구현은 멤버를 `bool`로 두고 `std::atomic_ref<bool>`로 접근했으나, CI의 Emscripten 3.1.74에 든 libc++ 18에 `atomic_ref`가 없어(libc++ 19부터) wasm32 빌드가 깨졌다. 소비자도 각자의 emsdk로 코어를 빌드하므로 CI의 emsdk를 올리는 대신 코드를 바꿨다(2026-10-10). `RaiseInterrupt`는 `Cpu`를 돌리는 스레드에서만 불리므로(`pending_interrupts_`가 원자적이지 않다) relaxed로 세운다. `RequestStop`은 요청을 먼저 쓰고 `attention`을 세운다. `RefreshAttention`은 `attention`을 먼저 쓰고 요청을 다시 읽는다. 그래서 요청이 `attention` 없이 남는 순서는 없다.

자기 수정 코드는 바뀌지 않는다: `StepOne`이 명령마다 캐시를 조회하고, 조회는 페이지 세대를 비교하므로 블록 안에서 바로 다음 명령을 고쳐도 새 바이트를 디코드한다(#34 결정 2). 정확한 폴트도 명령마다 `ExecuteDecoded`가 지킨다.

*`interp::RunBlock(state, memory, environment, features, cache, limits)` calls `StepOne` back to back; a "block" here is the instructions run between `Cpu::Run`'s checks. It does not end at branches: nothing in the semantics needs it (a branch's target check completes inside the instruction), and ending there would only add loop round trips. It returns when an instruction does not simply retire (stop, fault, unimplemented: `Run` returns the event as today), at `limits.max_instructions` (what remains of the budget and of the decode cache's warm-up, #34), when `*limits.attention` is raised (a stop request or a pending interrupt, which `Run` handles), or when the next instruction may be at a gate (the gate filter of decision 3 is positive and `Run` checks the exact set). `attention` is an atomic flag in `Cpu` (below), raised by `RaiseInterrupt` and `RequestStop` and recomputed by `RefreshAttention` after `ClearPendingInterrupt` and after a stop request is taken (true while an interrupt is pending or a stop is requested). An interrupt raised or a stop requested from a host callback during an instruction (ports, CPUID and so on) returns to `Run` at the next boundary, as today. With IF clear and an interrupt pending, `attention` stays raised and blocks run one instruction each, today's speed with today's meaning. `Run` takes the interrupt shadow (MOV SS, STI) from the block's last instruction; if the instruction after a shadow ran inside the block, the shadow had passed, and `attention` being clear then, no interrupt was waiting. The public header says `RequestStop` may come from another host thread, so `stop_requested_` and `attention_` are `Cpu::AtomicFlag`s wrapping `std::atomic<bool>`, whose move copies the value so `Cpu` stays movable (`std::atomic` cannot move, and moving a `Cpu` another thread is using is a race anyway). The first implementation kept the members `bool` behind `std::atomic_ref<bool>`, which broke the wasm32 build: CI's Emscripten 3.1.74 ships libc++ 18, which lacks `atomic_ref` (libc++ 19 adds it); the consumers build the core with their own emsdk, so the code changed rather than CI's emsdk (2026-10-10). `RaiseInterrupt` runs on the thread running the `Cpu` only (`pending_interrupts_` is not atomic) and raises the flag relaxed. `RequestStop` stores the request before raising `attention`, and `RefreshAttention` stores `attention` before reading the request again, so no order leaves a request without `attention`. Self-modifying code is unchanged: `StepOne` looks up the cache for every instruction and the lookup compares page generations, so rewriting the very next instruction inside a block decodes the new bytes (#34 decision 2); precise faults stay per instruction in `ExecuteDecoded`.*

## 결정 3: 게이트 필터 / Decision 3: the gate filter

게이트는 `std::unordered_set`이고 명령마다 해시 조회가 6% 들었다. `Cpu`가 게이트마다 65,536비트(8 KiB) 필터에 비트 하나를 세운다(`Bit(linear) = (linear × 0x9E3779B1) >> 16`). 블록 루프는 다음 명령의 선형 주소로 비트를 보고, 꺼져 있으면 게이트가 아님이 확실하므로 계속하고, 켜져 있으면 돌아가서 `Run`이 정확한 집합으로 확인한다(거짓 양성은 블록 하나를 끊을 뿐이다). `UnregisterGate`는 비트를 공유할 수 있으므로 남은 게이트로 필터를 다시 만든다. 게이트가 없으면 필터는 비어 있고(메모리 0), 블록 루프는 검사를 건너뛴다.

*Gates are a `std::unordered_set`, and looking one up per instruction cost 6%. `Cpu` sets one bit per gate in a 65,536-bit (8 KiB) filter (`Bit(linear) = (linear × 0x9E3779B1) >> 16`). The block loop tests the next instruction's linear address: a clear bit proves no gate is there and the block goes on; a set bit returns to `Run`, which checks the exact set (a false positive only ends one block). `UnregisterGate` rebuilds the filter from the remaining gates since bits may be shared. Without gates the filter is empty (no memory) and the block loop skips the test.*

## 결정 4: `SlotOf`를 표로 / Decision 4: `SlotOf` as a table

Zydis는 AL부터 EDI까지 레지스터 id를 연속으로 매긴다. 기존 switch를 `constexpr`로 두고 그것으로 컴파일 시간에 표를 만들어, `SlotOf`는 범위 검사와 표 조회 하나가 된다. 매핑의 정의는 switch 하나로 남는다.

*Zydis numbers the registers AL through EDI contiguously. The existing switch becomes `constexpr` and builds a table at compile time, so `SlotOf` is one range check and one table read, the mapping still defined by that single switch.*

## 결정 5: 무엇이 바뀌지 않아야 하나 / Decision 5: what must not change

결과와 호스트가 보는 동작은 바뀌지 않는다. 공개 API도 그대로다(`Cpu`에 비공개 멤버 `gate_filter_`와 `attention_`이 더해져 크기와 ABI가 바뀌므로 소비자는 다시 빌드한다). 직접 `interp::Step`을 부르는 SST 러너와 단위 테스트는 바뀌지 않는다.

검증은 기존 수단 전부(단위 테스트, 세 호스트 대조 fuzz, trace 묶음 넷, 견고성 하네스, 캐시 상시 빌드 포함)와 블록 루프 전용 단위 테스트(지시서 항목 4)다. 대조 fuzz와 견고성 하네스는 `Cpu::Run`을 거치므로 블록 루프를 지난다.

*Results and host-visible behavior do not change, nor does the public API (`Cpu` gains the private members `gate_filter_` and `attention_`, changing its size and ABI, so consumers rebuild); the SST runner and the unit tests calling `interp::Step` directly are unaffected. Verification is every existing instrument (unit tests, the three host-comparison fuzzes, the four trace corpora, the robustness harness, cache-always builds included) plus the block loop's own unit tests (work order item 4); the comparison fuzzes and the robustness harness go through `Cpu::Run` and so through the block loop.*

## 소비자 영향 / Consumer impact

* 다시 빌드만 필요하다. 동작 계약은 같다.
* 게이트를 많이 등록하는 소비자(re2DJ의 import 썽크)는 필터 8 KiB가 `Cpu`마다 더 든다. 게이트 해제는 남은 게이트 수에 비례하는 재구성 비용이 든다(해제는 드물다).

*A rebuild only, with the same behavioral contract. Consumers registering gates (re2DJ's import thunks) pay 8 KiB of filter per `Cpu`, and unregistering a gate costs a rebuild proportional to the remaining gates (unregistering is rare).*
