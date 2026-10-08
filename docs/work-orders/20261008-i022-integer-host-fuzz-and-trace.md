# #22 작업 지시서 : P6 정수 보완과 32비트 정수 명령 호스트 CPU 대조, trace와 호스트 간 재생 / #22 work order : P6 integer completion, the 32-bit integer host-CPU comparison, traces and cross-host replay

이슈: [#22](https://github.com/reexec/rex86/issues/22) | 설계: [20261008-i022](../design/20261008-i022-integer-host-fuzz-and-trace.md) | 로그: [20261008-i022](../work-logs/20261008-i022-integer-host-fuzz-and-trace.md)

## 절차 / Steps

1. `src/trace/`: trace 형식(쓰기, 읽기)과 재생(코어로 실행해 비교). `src/tools/trace/`: `rex86_trace`(재생, `--dump`). 형식 왕복 단위 테스트.
   *The trace format (write, read) and replay in `src/trace/`; `rex86_trace` (replay, `--dump`) in `src/tools/trace/`; a round-trip unit test.*
2. `tests/host/linux/int_fuzz.cpp`: 형태 표, 생성기와 주소 맞춤, TF 단일 스텝 호스트 러너(신호, 대체 스택, REP 재개), 코어 러너, 미정의 마스크, `--replay`, `--record`.
   *The integer fuzz: form table, generator with address fixups, the TF single-step host runner (signals, alternate stack, REP resumption), the core runner, undefined masks, `--replay`, `--record`.*
3. 코어: `exec_post386.cpp`(CMOVcc, BSWAP, XADD, CMPXCHG, CMPXCHG8B, UD0/1/2), `Features::cmov`, POPF/IRET의 AC/ID. 단위 테스트.
   *Core: `exec_post386.cpp`, `Features::cmov`, AC/ID in POPF/IRET; unit tests.*
4. fuzz를 돌려 불일치를 0으로(SDM 기준 수정, 미정의 마스크, `vendor_deviations`). SST 회귀 실행.
   *Run the fuzz down to zero mismatches (SDM fixes, undefined masks, `vendor_deviations`); rerun SST.*
5. x87 fuzz `--record`. `tests/traces/`에 고정 시드 묶음. CMake: `rex86_trace_format`, `rex86_trace`(Emscripten은 `-sNODERAWFS=1`), `rex86_int_fuzz`(i386 Linux만), ctest 등록. CI: 재생, i386 정수 fuzz, `linux-x64-sanitize`.
   *x87 fuzz `--record`; fixed-seed corpus in `tests/traces/`; CMake targets and ctest; CI replay, the i386 integer fuzz and `linux-x64-sanitize`.*
6. 문서: analysis(정수 호스트 대조), kb(TF 단일 스텝 기법), 가이드(정수 fuzz와 trace), ARCHITECTURE, README, 작업 로그. 로컬 빌드(GCC, Clang, i386)와 push 뒤 CI.
   *Docs, local builds (GCC, Clang, i386) and CI after the push.*

## 완료 조건 / Done when

정수 fuzz가 여러 시드에서 불일치 0, 저장소의 trace 묶음이 다섯 호스트 CI에서 불일치 0, SST 수치 유지, sanitizer 작업 녹색, 단위 테스트 통과.

*Zero integer-fuzz mismatches over several seeds, zero corpus mismatches on all five CI hosts, SST figures unchanged, the sanitizer job green, unit tests passing.*
