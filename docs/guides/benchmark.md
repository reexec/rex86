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

   옵션: `--frames N`, `--budget N`(프레임당 명령 수), `--ipc X`(기판 환산의 IPC 가정, 기본 1.0), `--only alu,x87`(워크로드 선택), `--engine interpreter`(지금은 인터프리터만. 다른 값은 `engine_available=false`로 거절), `--dump <디렉터리>`(워크로드 이미지 `image.bin`과 메타데이터 `image.txt`를 씁니다. 비교군 측정용).

3. **줄을 읽습니다.**
   * `workload=... mips=... frame_ms_p50= frame_ms_p99= frame_ms_max= first_frame_ms= laps= lap_instructions= verified=ok`: 워크로드 하나의 측정값. `verified=fail`이면 코어가 틀린 결과를 냈거나 폴트가 났다는 뜻이고 `reason=`이 설명합니다. 그 수치는 쓰지 않습니다.
   * `workload=... board=... frame_instructions= frame_ms_at_board= realtime_ratio=`: 기판 넷(`ez2dj1`, `mk3`, `mk5`, `ez2dj2`)에 대한 환산. `realtime_ratio` 1.0이 실시간이고, `frame_ms_at_board`는 기판 한 프레임 분량(클럭 × IPC / 60)을 측정한 MIPS로 돌릴 때 걸리는 시간입니다. 16.7 ms 아래가 목표입니다.
   * `memory guest_bytes= page_table_bytes= cpu_bytes=`: 목표 7의 계측. 코드 캐시 사용량은 번역 백엔드와 함께 더해집니다.
   * `string` 워크로드의 MIPS는 다른 워크로드와 비교하지 않습니다. REP 명령 하나가 64 dword 블록 전체를 처리하고 명령 하나로 셉니다. 프레임 시간으로 비교합니다.
   * IPC 1.0은 **추정**입니다(설계 결정 4). 기준 CPU에 유리한 가정이므로 코어에는 엄격한 쪽입니다.

4. **기록합니다.** 호스트(CPU 모델, OS), 컴파일러와 구성, 커밋, 전체 출력을 `docs/analysis/interpreter-performance.md`에 적습니다. 하네스는 호스트 CPU 모델을 읽지 않으므로 `/proc/cpuinfo`나 시스템 정보를 따로 봅니다. 수치는 측정한 호스트에서만 뜻이 있고, CI 러너의 수치는 변동이 커 기록하지 않습니다.

5. **비교군 측정(후속)**: `--dump`로 내보낸 `image.bin`을 `image.txt`가 적는 주소에 적재하고 `entry`에서 시작해 `gate` 주소에 닿을 때까지 돌리면 같은 워크로드입니다. `expected_edi`로 결과를 확인합니다.

*Build Release (Debug figures mean nothing; the first line's `config=` says which), run the defaults on an idle host (a million instructions a frame, 20 frames, six workloads, one to two minutes on the interpreter) with options `--frames`, `--budget`, `--ipc` (the IPC assumption of the board conversion, default 1.0), `--only`, `--engine` (the interpreter alone today; anything else is refused with `engine_available=false`) and `--dump <dir>` (writes `image.bin` and `image.txt` for the comparator measurement). Read the lines: a workload line with `verified=ok` carries its figures, `verified=fail` means a wrong result or a fault explained by `reason=` and its figures are not used; the board lines convert to the four boards, `realtime_ratio` 1.0 being real time and `frame_ms_at_board` the time one board frame's worth (clock × IPC / 60) takes at the measured MIPS, 16.7 ms being the target; the `memory` line is goal 7's instrumentation; the `string` workload's MIPS does not compare with the others (one REP instruction moves a 64-dword block and counts once), so compare its frame time; IPC 1.0 is inferred (design decision 4) and strict on the core. Record host (CPU model, OS), compiler and configuration, commit and the full output in `docs/analysis/interpreter-performance.md`; the harness does not read the CPU model, so take it from `/proc/cpuinfo` or the system information; CI runner figures vary too much to record. For the comparator measurement (a follow-up), load the dumped `image.bin` at the address `image.txt` gives, start at `entry`, run until `gate` and check `expected_edi`.*
