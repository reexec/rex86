# #11 작업 지시 : 인터프리터 1차 / #11 work order : interpreter increment 1

이슈: [#11](https://github.com/reexec/rex86/issues/11) | 설계: [20261007-i011](../design/20261007-i011-interpreter-core.md) | 로그: [20261007-i011](../work-logs/20261007-i011-interpreter-core.md)

## 작업 항목 / Tasks

1. `src/interp/interpreter.{h,cpp}`: `interp::Step`(한 명령 실행), 주소 생성(세그먼트 base/limit, 주소 크기 wrap), 폭별 플래그 헬퍼, 설계 결정 3의 1차 명령 그룹, 미구현 mnemonic의 구분 상태.
2. `src/cpu.cpp`: `Run`을 실행 루프로(정지 요청, 게이트, 인터럽트 전달, 예산), `ActiveEngine`을 `kInterpreter`로. 공개 헤더 무변경.
3. `src/tools/sst/main.cpp`: `--execute` 모드(설계 결정 4). 통과/실패/건너뜀과 커버리지 요약.
4. `tests/unit/interp_test.cpp`: 설계 검증 절의 케이스. `tests/unit/cpu_test.cpp`: `kNoEngine` 기대를 새 동작으로 갱신.
5. `ARCHITECTURE.md`: interp/ 행 [구현됨](1차 범위 명시), probe 줄 변화 기록. `README.md` 달성도 표 갱신.
6. SST 실행 비교를 전체 스위트에 돌려 결과를 작업 로그에 기록.

*1 implement `interp::Step` with address generation, the width-parametric flag helpers, the first instruction groups and the distinguished unimplemented state; 2 turn `Cpu::Run` into the execution loop (stop requests, gates, interrupt delivery, budget) answering `kInterpreter`, with no public-header change; 3 add the runner's `--execute` mode with the pass/fail/skip and coverage summary; 4 add the interpreter unit tests and update cpu_test's `kNoEngine` expectations; 5 update ARCHITECTURE.md (interp/ implemented with the increment's scope, the probe line change) and README's attainment table; 6 run the SST comparison over the full suite and record the figures in the work log.*

## 완료 조건 / Completion criteria

* 구현된 그룹의 SST 실행 비교 불일치 0. 미구현·예외·세그먼트 테스트는 건너뜀으로 집계.
* 단위 테스트 전체 통과, 로컬 Windows x86 빌드 깨끗, 공개 계약 헤더 무변경.
* 나머지 호스트는 push 시 CI로 확인.

*Zero mismatches on the implemented groups' SST comparison with the skips tallied; all unit tests pass on a clean local Windows x86 build; the public contract headers are unchanged; CI checks the other hosts on push.*
