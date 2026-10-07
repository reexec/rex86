# #3 설계 : README 목표치와 달성도 절 / #3 design : README goals and attainment section

이슈: [#3](https://github.com/reexec/rex86/issues/3) | 지시서: [20261007-i003](../work-orders/20261007-i003-readme-goals-and-status.md) | 로그: [20261007-i003](../work-logs/20261007-i003-readme-goals-and-status.md)

## 요구사항 / Requirement

사용자는 README.md가 과제의 목표치와 달성도를 담기를 요구했다. 조건은 다섯이다.

1. 성능은 실용적으로 달성되어야 하고, 비교군이 있으면 비교군과 동등 이상을 목표로 한다.
2. 필요한 CPU/FPU 명령을 모두 지원한다.
3. JIT/AOT 등 성능 개선에 필요한 현대적 기술을 모두 지원한다.
4. 이 프로젝트 단독으로 달성도를 측정할 수 있어야 한다.
5. 웹을 통한 실행 예제 제공까지가 목표다.

*The user requires README.md to state the project's targets and attainment, under five conditions: performance must be practically achieved and match or beat comparators where they exist; every needed CPU/FPU instruction is supported; every modern performance technique needed (JIT/AOT, …) is supported; attainment is measurable by this project alone; and a web demo is part of the goal.*

## 설계 결정 / Design decisions

* 목표를 다섯 절(명령 지원, 정확성, 성능, 단독 측정 가능성, 웹 예제)로 나누고, 각 절에 측정 수단과 완료 기준을 명시한다. 정확성은 요구사항에 명시되지 않았지만 [AGENTS.md](../../AGENTS.md)의 1·2번 설계 목표이므로 목표 절에 포함한다.
* 성능의 실용 기준은 "중급 모바일 브라우저와 ARM 기기에서 원본 하드웨어(Pentium II~III급) 실시간 속도, 16.7ms 프레임 예산"으로 적는다. 수치(MIPS) 목표는 census와 2단계 측정 전에는 근거가 없으므로("측정 없이 최적화하지 않는다") 측정 뒤 이 절에 확정하기로 적어 둔다.
* 비교군은 공개 구현(v86, Boxedwine 등)으로 두되, 비교는 벤치마크 결과로만 하고 전염성 라이선스 구현의 소스는 참고하지 않는다는 기존 규칙을 같은 절에 반복한다. 비교군 확정은 벤치마크 하네스 작업에서 다룬다.
* 현대적 기술은 wasm/AArch64 JIT 백엔드, 블록 체이닝, lazy flags, 코드 캐시, SMC 페이지 속성표 검사를 명시하고, AOT는 [설계 #1](20261007-i001-repository-and-public-contract.md)대로 2단계 측정 뒤 rePIU AOT 대체 판단과 함께 검토한다고 적는다. JIT 금지 호스트(iOS)는 인터프리터 전용 모드가 하한이다.
* 달성도는 설계 #1의 단계 계획(0~6)과 목표 번호를 잇는 표로 두고, 작업이 머지될 때마다 갱신한다. 벤치마크 하네스는 단계 표에 없던 산출물이므로 별도 행(—)으로 더한다. 현재 상태는 0단계 완료, 나머지 미착수, 명령 커버리지 0%로 적는다.
* 공개 계약과 코드는 바뀌지 않으므로 소비자 영향은 없다.

*The goals split into five sections (instruction coverage, correctness, performance, standalone measurability, web demo), each with its measuring instrument and completion bar; correctness, while not named in the requirement, is design goals 1–2 of [AGENTS.md](../../AGENTS.md) and belongs in the list. The practical performance bar reads "original-hardware (Pentium II–III class) real-time speed within the 16.7 ms frame budget on a mid-range mobile browser and ARM device"; numeric MIPS targets have no basis before the census and the phase 2 measurements ("nothing is optimized without a measurement") and are fixed into the section afterwards. Comparators are public implementations (v86, Boxedwine, …), benchmark-only, repeating the rule that no copyleft source is consulted; their final choice belongs to the benchmark-harness task. The modern techniques named are the wasm/AArch64 JIT backends, block chaining, lazy flags, the code cache and page-attribute SMC checks, with AOT evaluated after phase 2 as [design #1](20261007-i001-repository-and-public-contract.md) states and the interpreter-only mode as the floor on JIT-forbidden hosts. Attainment is a table joining design #1's phases (0–6) to the goal numbers, updated as work merges, with one extra row (—) for the benchmark harness, currently showing phase 0 done, the rest not started, and 0% instruction coverage. The public contract and code do not change, so there is no consumer impact.*
