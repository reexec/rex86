# #31 작업 지시 : 견고성 하네스 / #31 work order : the robustness harness

이슈: [#31](https://github.com/reexec/rex86/issues/31) | 설계: [20261009-i031](../design/20261009-i031-robustness-fuzz.md) | 로그: [20261009-i031](../work-logs/20261009-i031-robustness-fuzz.md)

## 작업 항목 / Tasks

1. `src/tools/robust/robust_case.{h,cpp}`: 난수원(시드 또는 libFuzzer 바이트), 케이스 생성(메모리, 보호 구역, 페이지 속성, 상태, 기능, 환경), 실행과 호스트 개입, 불변식 I2~I6, 디코더 fuzz(설계 결정 1, 2).
2. `src/tools/robust/main.cpp`: 드라이버 `rex86_robust`(`--cases`, `--seed`, `--case`, `--verbose`), 요약 줄.
3. `src/tools/robust/libfuzzer.cpp`와 CMake 옵션 `REX86_LIBFUZZER`(결정 3).
4. CMake: 모든 호스트에서 빌드, ctest `rex86_robust_smoke`. CI 작업 `linux-x64-libfuzzer`.
5. 단위 테스트 `tests/unit/robust_test.cpp`: 불변식 검사기가 위반을 잡는지, 같은 시드가 같은 케이스를 만드는지.
6. 긴 실행과 libFuzzer 실행. 찾은 결함을 고치고 단위 테스트로 고정한다(결정 5).
7. 문서: 분석 `robustness-fuzz.md`와 색인, 가이드 `robustness-fuzz.md`, ARCHITECTURE, README 달성도(견고성 행), 작업 로그. REP 반응성 후속 이슈를 만든다(결정 4).

*1 `robust_case.{h,cpp}`: the random source (seed or libFuzzer bytes), case generation, execution with host interventions, invariants I2-I6, the decoder fuzz; 2 the driver `rex86_robust` and its summary; 3 the libFuzzer entry and `REX86_LIBFUZZER`; 4 CMake on every host, ctest `rex86_robust_smoke`, the CI job `linux-x64-libfuzzer`; 5 unit tests that the checkers catch violations and that a seed makes the same case; 6 long runs and libFuzzer, fixing and pinning what they find; 7 the analysis and guide topics, ARCHITECTURE, README's robustness row, the work log, and the follow-up issue for REP responsiveness.*

## 완료 조건 / Completion criteria

* 드라이버 긴 실행(ASan/UBSan 포함)과 libFuzzer 장시간 실행에서 위반과 새니타이저 보고가 0이다.
* 찾은 결함마다 단위 테스트가 있다.
* 다섯 호스트 CI와 새 libFuzzer 작업이 녹색이고, 공개 계약은 바뀌지 않는다.

*Zero violations and sanitizer reports in long driver runs (ASan/UBSan included) and long libFuzzer runs; a unit test for every defect found; green CI on five hosts plus the new libFuzzer job, with no contract change.*
