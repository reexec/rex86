# #13 작업 지시 : 인터프리터 2차 / #13 work order : interpreter increment 2

이슈: [#13](https://github.com/reexec/rex86/issues/13) | 설계: [20261007-i013](../design/20261007-i013-interpreter-increment-2.md) | 로그: [20261007-i013](../work-logs/20261007-i013-interpreter-increment-2.md)

## 작업 항목 / Tasks

1. `src/interp/`: 설계 범위 표의 그룹을 구현한다. 책임이 커지는 부분은 파일로 분리한다(시프트/회전과 곱셈/나눗셈은 `arith2.cpp`, 문자열은 `strings.cpp` 같은 형태로, 기존 `interpreter.cpp`는 dispatch 유지).
2. `tests/unit/interp_test.cpp`(또는 분리 파일): 설계 검증 절의 그룹별 대표 케이스.
3. SST 실행 비교를 전체 스위트에 돌려 mismatches 0 확인, 커버리지 수치를 로그에 기록.
4. `ARCHITECTURE.md`의 interp/ 행에 2차 범위 반영, README 달성도 표 갱신.

## 완료 조건 / Completion criteria

* 전체 SST 실행 비교 mismatches 0, 구현 그룹이 skipped_unimplemented에서 빠져 executed로 이동.
* 단위 테스트 전체 통과, 로컬 Windows x86 빌드 깨끗, 공개 계약 무변경.

*Implement the design's groups under `src/interp/`, splitting growing responsibilities into files (shifts/multiply into an arith file, strings into their own, `interpreter.cpp` keeping the dispatch); add the representative unit cases; run the full SST comparison to zero mismatches with the coverage recorded; update ARCHITECTURE.md and the README attainment table. Done when the full comparison stays at zero mismatches with the new groups moved from skipped to executed, all unit tests pass on a clean local Windows x86 build, and the public contract is unchanged.*
