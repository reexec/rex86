# 가이드 : 벤치마크 하네스 / Guide : the benchmark harness

근거: [#27 설계](../design/20261009-i027-benchmark-harness.md) | 로그: [20261009-i027](../work-logs/20261009-i027-benchmark-harness.md) | 측정 기록: [인터프리터 성능](../analysis/interpreter-performance.md)

`rex86_bench`는 실행 중에 조립한 합성 워크로드 여섯을 코어에서 프레임 단위로 돌려 MIPS, 프레임 시간 분포, 첫 프레임까지의 시간, 대상 기판 기준 CPU 대비 실시간 비율을 `[rex86-bench] key=value` 줄로 보고합니다. 모든 호스트에서 빌드되고, 결과는 C++ 참조 모델과 대조되어 틀린 결과를 성능으로 치지 않습니다. CTest는 `--smoke`(워크로드마다 검증된 두 바퀴, 시간 측정 없음)를 등록합니다.

*`rex86_bench` runs six synthetic workloads, assembled at run time, on the core frame by frame and reports MIPS, the frame-time distribution, time to first frame and the real-time ratio against the target boards' reference CPUs as `[rex86-bench] key=value` lines. It builds on every host; results are checked against a C++ reference model, so a wrong result never counts as performance. CTest registers `--smoke` (two verified laps per workload, no timing).*

## 절차 / Procedure

1. **Release로 빌드합니다.** Debug 수치는 뜻이 없습니다. 첫 줄의 `config=`로 구성을 확인합니다.

   ```bash
   cmake --preset linux-x64-release && cmake --build --preset linux-x64-release
   # Windows: cmake --preset windows-x86-debug && cmake --build --preset windows-x86-release
   # wasm32:  scripts/build_web_wasm.sh  (node build/web-wasm-release/bin/rex86_bench.js)
   ```

2. **다른 부하가 없는 상태에서 기본 설정으로 돌립니다.** 프레임 예산 100만 명령, 20프레임, 워크로드 여섯. 인터프리터에서 1~2분 걸립니다.

   ```bash
   ./build/linux-x64-release/bin/rex86_bench | tee bench.txt
   ```

   옵션: `--frames N`, `--budget N`(프레임당 단계 수), `--ipc X`(기판 환산의 IPC 가정, 기본 1.0), `--only alu,x87`(워크로드 선택), `--engine interpreter`(지금은 인터프리터만. 다른 값은 `engine_available=false`로 거절), `--dump <디렉터리>`(워크로드 이미지 `image.bin`과 메타데이터 `image.txt`를 씁니다. 비교군 측정용).

3. **줄을 읽습니다.**
   * `workload=... mips=... frame_ms_p50= frame_ms_p99= frame_ms_max= first_frame_ms= laps= lap_instructions= verified=ok`: 워크로드 하나의 측정값. `verified=fail`이면 코어가 틀린 결과를 냈거나 폴트가 났다는 뜻이고 `reason=`이 설명합니다. 그 수치는 쓰지 않습니다.
   * `workload=... board=... frame_instructions= frame_ms_at_board= realtime_ratio=`: 기판 넷(`ez2dj1`, `mk3`, `mk5`, `ez2dj2`)에 대한 환산. `realtime_ratio` 1.0이 실시간이고, `frame_ms_at_board`는 기판 한 프레임 분량(클럭 × IPC / 60)을 측정한 MIPS로 돌릴 때 걸리는 시간입니다. 16.7 ms 아래가 목표입니다.
   * `memory guest_bytes= page_table_bytes= cpu_bytes=`: 목표 7의 계측. 코드 캐시 사용량은 번역 백엔드와 함께 더해집니다.
   * 예산과 `retired=`, `lap_instructions=`, `mips=`는 **단계**를 셉니다(#32). 단계는 명령 하나, REP 문자열에서는 반복 하나입니다. 그래서 `string` 워크로드(REP STOSD/MOVSD가 64 dword씩)도 다른 워크로드와 같은 척도입니다. #32 이전의 기록은 REP를 명령 하나로 셌으므로 `string`과 `mixed`는 그 기록과 바로 비교하지 않습니다.
   * IPC 1.0은 **추정**입니다(설계 결정 4). 기준 CPU에 유리한 가정이므로 코어에는 엄격한 쪽입니다.

4. **기록합니다.** 호스트(CPU 모델, OS), 컴파일러와 구성, 커밋, 전체 출력을 `docs/analysis/interpreter-performance.md`에 적습니다. 하네스는 호스트 CPU 모델을 읽지 않으므로 `/proc/cpuinfo`나 시스템 정보를 따로 봅니다. 수치는 측정한 호스트에서만 뜻이 있고, CI 러너의 수치는 변동이 커 기록하지 않습니다.

5. **비교군 측정(후속)**: `--dump`로 내보낸 `image.bin`을 `image.txt`가 적는 주소에 적재하고 `entry`에서 시작해 `gate` 주소에 닿을 때까지 돌리면 같은 워크로드입니다. `expected_edi`로 결과를 확인합니다.

*Build Release (Debug figures mean nothing; the first line's `config=` says which), run the defaults on an idle host (a million steps a frame, 20 frames, six workloads, one to two minutes on the interpreter) with options `--frames`, `--budget`, `--ipc` (the IPC assumption of the board conversion, default 1.0), `--only`, `--engine` (the interpreter alone today; anything else is refused with `engine_available=false`) and `--dump <dir>` (writes `image.bin` and `image.txt` for the comparator measurement). Read the lines: a workload line with `verified=ok` carries its figures, `verified=fail` means a wrong result or a fault explained by `reason=` and its figures are not used; the board lines convert to the four boards, `realtime_ratio` 1.0 being real time and `frame_ms_at_board` the time one board frame's worth (clock × IPC / 60) takes at the measured MIPS, 16.7 ms being the target; the `memory` line is goal 7's instrumentation; the budget, `retired=`, `lap_instructions=` and `mips=` count **steps** (#32), a step being one instruction or one REP string iteration, so the `string` workload (REP STOSD/MOVSD over 64 dwords) shares the others' scale, while records before #32 counted a REP once and their `string` and `mixed` figures do not compare directly; IPC 1.0 is inferred (design decision 4) and strict on the core. Record host (CPU model, OS), compiler and configuration, commit and the full output in `docs/analysis/interpreter-performance.md`; the harness does not read the CPU model, so take it from `/proc/cpuinfo` or the system information; CI runner figures vary too much to record. For the comparator measurement (a follow-up), load the dumped `image.bin` at the address `image.txt` gives, start at `entry`, run until `gate` and check `expected_edi`.*

## 전후 비교와 프로파일 / Before-and-after comparisons and profiling

* **전후는 번갈아 잽니다.** 같은 기계라도 호스트 상태에 따라 같은 바이너리가 30% 넘게 달라질 수 있습니다(#35에서 Windows 빌드). 기준선을 별도 디렉터리에 빌드하고(예: `git archive`한 소스) 전, 후, 전, 후 순서로 세 번 이상 돌려 중앙값과 배율을 적습니다. Linux에서는 `taskset -c N`으로 코어를 고정합니다.
* **프로파일**: 이 환경에는 perf와 valgrind가 없고, gprof의 줄 단위 모드(`-l`)는 이 바이너리에서 깨집니다. gprof의 함수 단위 수치는 인라인된 비용을 나누지 못해 #34에서 원인을 잘못 읽었습니다(분석 2.2절). 줄 단위가 필요하면 작업 중에만 쓰는 표본 프로파일러를 씁니다: Linux는 `LD_PRELOAD` 라이브러리가 `SIGPROF` 타이머로 RIP를 모으고 `addr2line -f -C`로 인라인된 함수와 줄을 찾습니다(`-O3 -g` 빌드). Windows는 실행기가 자식을 1 ms마다 멈춰 `GetThreadContext`로 EIP를 읽고 DbgHelp(Visual Studio에 들어 있는 최신 x86 `dbghelp.dll`을 실행기 옆에 둠)로 PDB에서 찾습니다(`/Zi`, `/DEBUG /DYNAMICBASE:NO` 빌드, `SYMOPT_DEFERRED_LOADS`를 끔). 두 도구는 저장소에 넣지 않았습니다.

*Interleave before and after: on one machine the same binary can vary by more than 30% with host state (the Windows build in #35), so build the baseline in its own directory (from `git archive`d source, say), run before, after, before, after at least three times and record medians and ratios, pinning a core with `taskset -c N` on Linux. Profiling: there is no perf or valgrind here and gprof's line mode (`-l`) breaks on this binary, while gprof's per-function figures cannot apportion inlined cost, which misled #34 (analysis section 2.2). For line-level profiles use scratch sampling profilers: on Linux an `LD_PRELOAD` library collecting RIP on a `SIGPROF` timer, resolved by `addr2line -f -C` to inlined functions and lines (an `-O3 -g` build); on Windows a launcher suspending the child every millisecond to read EIP with `GetThreadContext`, resolved from the PDB through DbgHelp (a recent x86 `dbghelp.dll` from Visual Studio placed beside the launcher; a `/Zi`, `/DEBUG /DYNAMICBASE:NO` build; `SYMOPT_DEFERRED_LOADS` off). Neither tool is in the repository.*
