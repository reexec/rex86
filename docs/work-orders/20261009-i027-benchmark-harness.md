# #27 작업 지시 : 벤치마크 하네스 / #27 work order : the benchmark harness

이슈: [#27](https://github.com/reexec/rex86/issues/27) | 설계: [20261009-i027](../design/20261009-i027-benchmark-harness.md) | 로그: [20261009-i027](../work-logs/20261009-i027-benchmark-harness.md)

## 작업 항목 / Tasks

1. `src/tools/bench/asm.{h,cpp}`: 커널이 쓰는 인코딩만 아는 바이트 어셈블러. 레이블, rel8/rel32 분기 고정, ModRM/SIB 도우미(설계 결정 2).
2. `src/tools/bench/workloads.{h,cpp}`: 커널 여섯(`alu`, `memory`, `call`, `string`, `x87`, `mixed`)과 드라이버, 메모리 배치, 초기 상태, 데이터 초기화, C++ 참조 모델(결정 2).
3. `src/tools/bench/runner.{h,cpp}`: `Environment` 구현, 프레임 루프(게이트에서 검증과 리셋, 남은 예산 이어 실행), 통계(MIPS, p50/p99/max, 첫 프레임), 기판 환산표(결정 3, 4, 6).
4. `src/tools/bench/main.cpp`: 인자(`--smoke`, `--frames`, `--budget`, `--ipc`, `--engine`, `--only`, `--dump`), key=value 출력(결정 5).
5. `CMakeLists.txt`: `rex86_bench` 타깃(모든 호스트, wasm32는 `-sNODERAWFS`로 `--dump` 지원), 구성 이름 컴파일 정의, ctest `rex86_bench_smoke`. `CMakePresets.json`에 `windows-x86-release` 빌드 프리셋.
6. `tests/unit/bench_test.cpp`: 설계의 검증 절.
7. 측정: WSL2 GCC x86-64 Release와 Windows MSVC x86 Release에서 기본 설정으로 돌려 `docs/analysis/interpreter-performance.md`에 기록. 분석 색인 갱신.
8. 문서: 가이드 `docs/guides/benchmark.md`, ARCHITECTURE 3절(도구)과 4절(빌드), README 달성도 표와 목표 3의 현재 수치, 작업 로그.

*1 `src/tools/bench/asm.{h,cpp}`, a byte assembler knowing only the kernels' encodings, with labels, rel8/rel32 fixups and ModRM/SIB helpers (design decision 2); 2 `workloads.{h,cpp}`, the six kernels and the driver, the layout, the initial state, data initialization and the C++ reference model (decision 2); 3 `runner.{h,cpp}`, the `Environment`, the frame loop (verify and reset at the gate, continue with the remaining budget), the statistics (MIPS, p50/p99/max, first frame) and the board table (decisions 3, 4, 6); 4 `main.cpp`, the arguments (`--smoke`, `--frames`, `--budget`, `--ipc`, `--engine`, `--only`, `--dump`) and key=value output (decision 5); 5 CMake: the `rex86_bench` target on every host (wasm32 with `-sNODERAWFS` for `--dump`), the configuration-name definition, the ctest entry `rex86_bench_smoke`, and the `windows-x86-release` build preset; 6 `tests/unit/bench_test.cpp` per the design's verification section; 7 measure on WSL2 GCC x86-64 Release and Windows MSVC x86 Release with the defaults into `docs/analysis/interpreter-performance.md`, updating the analysis index; 8 documents: the guide `docs/guides/benchmark.md`, ARCHITECTURE sections 3 and 4, README's attainment table and goal 3's current figures, the work log.*

## 완료 조건 / Completion criteria

* `rex86_bench`가 기본 설정으로 여섯 워크로드의 `verified=ok`와 MIPS, 프레임 분포, 네 기판의 실시간 비율을 한 번의 실행으로 출력한다.
* `rex86_bench --smoke`가 ctest에 등록되어 단위 테스트와 함께 통과한다. 다섯 호스트 CI가 녹색이다.
* 두 호스트의 기준선이 분석 문서에 확인됨으로, IPC 가정이 추정으로 적혀 있다.
* 공개 헤더와 코어 소스는 바뀌지 않는다.

*One default run of `rex86_bench` prints `verified=ok`, MIPS, the frame distribution and the four boards' real-time ratios for the six workloads; `rex86_bench --smoke` is a ctest entry passing with the unit tests and CI is green on the five hosts; the two hosts' baselines are in the analysis as confirmed with the IPC assumption as inferred; no public header or core source changes.*
