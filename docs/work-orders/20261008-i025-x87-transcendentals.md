# #25 작업 지시 : x87 2차 — 초월함수 / #25 work order : x87 increment 2 — the transcendentals

이슈: [#25](https://github.com/reexec/rex86/issues/25) | 설계: [20261008-i025](../design/20261008-i025-x87-transcendentals.md) | 로그: [20261008-i025](../work-logs/20261008-i025-x87-transcendentals.md)

## 작업 항목 / Tasks

1. **측정 먼저**: WSL2(Intel Core i5-7200U)에서 x87 fuzz를 빌드한다. 작은 측정 프로그램으로 SDM이 정의하지 않은 동작을 확인한다: C0/C3/C2, C2 = 1일 때 FCOS의 C1, 정의역 밖 F2XM1과 FYL2XP1, FSIN/FCOS의 tiny 입력에서의 #U, FSINCOS/FPTAN의 마스크된 스택 오버플로 응답. 결과는 분석 문서에 적는다(설계 결정 3, 4).
2. `src/fpu/`: 정수 다배정도 축소(66비트 Pi), f128 급수(sin, cos, tan, atan, expm1, log1p 계열), 지수 분리 반올림(`softfloat_roundPackToExtF80`, 지수 조정 결과), 여덟 명령의 수치 함수와 특수값 표(결정 1~4). 새 파일 `x87_transcendental.{h,cpp}`와 필요하면 `f128_series.{h,cpp}`.
3. `src/interp/exec_x87_transcendental.cpp`: 스택, C1/C2, push와 pop, `ExecuteX87` 연결(결정 4).
4. `tests/unit/x87_transcendental_test.cpp`: 설계의 테스트 전략.
5. `tests/host/linux/x87_fuzz.cpp`: `Excluded()`에서 여덟 명령 해제, 정의역 가중 입력, 세 갈래 판정과 `within_tolerance` 집계, 일치 사례만 기록(결정 5).
6. 긴 호스트 대조 실행(i386, x86-64), trace 묶음 `x87.rxt` 재생성, SST 전체, 로컬 빌드(GCC, Clang, MSVC는 CI).
7. 문서: ARCHITECTURE, README 달성도, analysis `x87-host-comparison.md`, kb `x87.md`, 가이드 `x87-host-fuzz.md`, 작업 로그.

*1 measure first: build the x87 fuzz under WSL2 (Intel Core i5-7200U) and check what the SDM leaves undefined with small probes (C0/C3/C2, FCOS's C1 when C2 = 1, F2XM1 and FYL2XP1 out of domain, #U for tiny FSIN/FCOS inputs, the masked stack-overflow response of FSINCOS/FPTAN), recorded in the analysis (design decisions 3 and 4); 2 build in `src/fpu/` the integer multi-precision reduction (66-bit Pi), the f128 series (sin, cos, tan, atan, expm1, log1p families), the exponent-carrying rounding (`softfloat_roundPackToExtF80`, exponent-adjusted results) and the eight numeric functions with their special-value tables (decisions 1-4), in the new `x87_transcendental.{h,cpp}` and, where useful, `f128_series.{h,cpp}`; 3 add `src/interp/exec_x87_transcendental.cpp` with the stack, C1/C2, pushes and pops, wired into `ExecuteX87` (decision 4); 4 add `tests/unit/x87_transcendental_test.cpp` per the design's test strategy; 5 in `tests/host/linux/x87_fuzz.cpp`, release the eight from `Excluded()`, add domain-weighted inputs, the three-way judgment with a `within_tolerance` count, and record matches only (decision 5); 6 run the long host comparison (i386, x86-64), regenerate the `x87.rxt` corpus, run SST in full and build locally (GCC, Clang; MSVC in CI); 7 update ARCHITECTURE, README attainment, the analysis `x87-host-comparison.md`, the kb page `x87.md`, the guide `x87-host-fuzz.md` and the work log.*

## 완료 조건 / Completion criteria

* Intel 호스트 긴 실행에서 초월함수를 포함한 x87 대조의 불일치가 0이다. 허용 사례는 설계 결정 5의 한계 안에 들고, 비율과 분포를 분석에 적는다. 정의되지 않은 범주와 제조사 이탈은 근거와 함께 따로 센다.
* 단위 테스트가 전부 통과하고 SST mismatches 0을 유지한다. trace 묶음은 다섯 호스트에서 재생된다.
* 다섯 호스트 CI가 녹색이고, 공개 헤더는 바뀌지 않는다.

*Zero mismatches in a long x87 comparison including the transcendentals on the Intel host, tolerated cases within design decision 5's bounds with their share and distribution in the analysis, and any undefined or vendor-deviation class counted apart with evidence; all unit tests passing, SST at zero mismatches, and the trace corpus replaying on five hosts; green CI on all five hosts with no public-header change.*
