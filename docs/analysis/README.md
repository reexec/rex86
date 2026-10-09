# 분석 색인 / Analysis Index

이 디렉터리는 이 프로젝트가 **직접 확인한** 사실을 주제별로 누적한다. 호스트 CPU와 대조해 측정한 명령 의미, 호스트별 차이, 소비자 프로젝트의 게스트가 실제로 쓰는 명령 집합(census)이 여기 들어간다. 일반 기술 배경은 [`docs/kb/`](../kb/README.md)에 둔다.

*This directory accumulates facts **verified directly** by this project, organized by topic: instruction semantics measured against a host CPU, differences between hosts, and the instruction set a consumer's guest actually uses (the census). General background knowledge lives in [`docs/kb/`](../kb/README.md).*

## 표기 규칙 / Notation

모든 서술은 **확인됨 / 추정 / 미확정** 중 하나로 표기한다. 확인됨에는 검증 방법을, 추정에는 근거를, 미확정에는 확인 방법을 함께 적는다.

*Every statement is marked **confirmed**, **inferred** or **unresolved**, alongside the verification method, the evidence, or the way to find out.*

## 문서 / Documents

| 문서 | 내용 | 현재 상태 |
| --- | --- | --- |
| [interpreter-performance.md](interpreter-performance.md) | 벤치마크 하네스 `rex86_bench`로 잰 인터프리터 처리량(MIPS), 프레임 시간, 대상 기판 기준 실시간 비율의 기준선. IPC 가정은 추정 / the interpreter's throughput (MIPS), frame times and per-board real-time ratios measured by the benchmark harness `rex86_bench`; the IPC assumption is inferred | #27에서 첫 기준선(AMD Zen 3 x86-64 GCC, Windows x86 MSVC), #34 디코드 캐시, #35 블록 루프와 #34 분석의 정정 / first baseline in #27 (AMD Zen 3 x86-64 GCC, Windows x86 MSVC), the decode cache in #34, the block loop and a correction to #34's reading in #35 |
| [integer-host-comparison.md](integer-host-comparison.md) | 32비트 정수 명령 호스트 CPU 대조(Intel Cascade Lake) 실측: 읽기 전용 세그먼트 쓰기, CR2, ENTER, CMPS와 BOUND의 순서, REP CMPS 폴트 플래그의 세대 차이 / the 32-bit integer host-CPU comparison (Intel Cascade Lake): read-only segment writes, CR2, ENTER, CMPS and BOUND ordering, a generation difference in faulting REP CMPS flags | #22에서 확인(Intel 1,000만 건 불일치 0, AMD는 CI 5만 건) / confirmed in #22 (10M cases on Intel with zero mismatches, 50,000 on AMD in CI) |
| [x87-host-comparison.md](x87-host-comparison.md) | x87 호스트 CPU 대조(AMD Zen 3, Intel Cascade Lake) 실측: SDM이 정하지 않은 동작, FPREM 부분 감소 N, 예외 우선순위, 제조사의 SDM 이탈 2종 / the x87 host-CPU comparison (AMD Zen 3, Intel Cascade Lake): behavior the SDM leaves open, FPREM's partial reduction, exception priority, two vendor deviations from the SDM | #19(AMD 4,000만 건)와 #22(Intel 2,000만 건)에서 불일치 0 / zero mismatches in #19 (AMD, 40M) and #22 (Intel, 20M) |
| [robustness-fuzz.md](robustness-fuzz.md) | 견고성 하네스(`rex86_robust`, libFuzzer)의 실행 규모와 결과, REP 정지 시간 발견 / the robustness harness's runs and results (`rex86_robust`, libFuzzer), the REP stall finding | #31에서 확인(코어 결함 0, 위반 0) / confirmed in #31 (no core defect, zero violations) |
| [simd-host-comparison.md](simd-host-comparison.md) | MMX/SSE 호스트 CPU 대조(AMD Zen 3) 실측: FXSAVE 이미지, EMMS의 TOP, CVTPI2PS의 MMX 전환, SSE의 반올림 전 언더플로 판정, #D의 우선순위, RCPPS/RSQRTPS 근사값 / the MMX/SSE host-CPU comparison (AMD Zen 3): the FXSAVE image, EMMS's TOP, CVTPI2PS's MMX transition, SSE's tininess before rounding, #D's precedence, the RCPPS/RSQRTPS approximations | #29에서 확인(2,200만 건 불일치 0). Intel과 P6 실물은 미확정 / confirmed in #29 (22M cases, zero mismatches); Intel and P6 hardware unresolved |
| [singlesteptests-386ex-deviations.md](singlesteptests-386ex-deviations.md) | SingleStepTests 386EX 실측: 테스트 구조, SDM과 다른 하드웨어 동작(SIB scale-on-base, POPAD ESP), 인코딩 해석 주의 / facts measured from the 386EX suite: structure, SDM deviations, encoding notes | #11에서 확인, #17에서 unreal 결론 정정과 예외 전달 추가 / confirmed in #11, corrected and extended in #17 |
