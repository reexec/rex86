# #35 작업 지시 : 블록 단위 실행 / #35 work order : block-level execution

이슈: [#35](https://github.com/reexec/rex86/issues/35) | 설계: [20261009-i035](../design/20261009-i035-block-execution.md) | 로그: [20261009-i035](../work-logs/20261009-i035-block-execution.md)

## 작업 항목 / Tasks

1. 측정 먼저: #34 위(HEAD)의 기준선을 같은 기계에서 다시 재고, 명령당 비용이 어디서 나는지 GCC와 MSVC 둘 다에서 표본 프로파일로 나눈다(gprof의 줄 단위 모드는 이 환경에서 깨지므로 임시 표본 프로파일러를 쓴다).
2. 측정이 가리키는 것만 고친다. 고친 것마다 두 컴파일러에서 전후를 번갈아 잰다. 이득이 잡음 범위 안이면 되돌린다.
3. 블록 루프: `interp::RunBlock`이 `Cpu::Run`의 검사 사이에서 명령을 이어 실행한다. 게이트, 외부 인터럽트, 정지 요청, 예산, 예열, SMC, 정확한 폴트의 의미를 그대로 지킨다(설계 결정 2, 3).
4. 단위 테스트: 직선 코드가 게이트로 흘러드는 경우, 필터의 거짓 양성, 게이트 해제 뒤 필터 재구성, 명령 중에 호스트가 올린 인터럽트와 정지 요청, IF가 꺼진 동안 대기하는 인터럽트와 STI 그림자, 블록 안의 예산, 블록 안의 SMC, 블록 중간의 폴트. 캐시가 없을 때와 있을 때 둘 다.
5. 검증: 단위 테스트, 세 호스트 대조 fuzz, trace 묶음, 견고성 하네스(ASan 포함), 로컬 빌드 전부(캐시 상시 빌드 포함). 벤치마크 전후.
6. 문서: 분석 `interpreter-performance.md`(전후, #34 분석의 정정, 남은 병목), ARCHITECTURE, README 현재 수치, 작업 로그.

*1 measure first: re-measure the baseline on #34 (HEAD) on the same machine and apportion the per-instruction cost by sampling profiles under both GCC and MSVC (gprof's line mode breaks here, so a scratch sampling profiler is used); 2 fix only what the measurement points to, measuring each change before and after, interleaved, under both compilers, and reverting what stays within the noise; 3 the block loop: `interp::RunBlock` runs instructions one after another between `Cpu::Run`'s checks, keeping the semantics of gates, external interrupts, stop requests, the budget, the warm-up, SMC and precise faults (design decisions 2 and 3); 4 unit tests for straight-line code falling into a gate, a filter false positive, the filter rebuilt after unregistering, an interrupt and a stop request raised by the host during an instruction, an interrupt waiting while IF is clear with STI's shadow, the budget inside a block, SMC inside a block and a fault mid-block, with and without the cache; 5 verification with every instrument (cache-always builds included) and the benchmark before and after; 6 the analysis (before and after, a correction to #34's reading, the remaining bottlenecks), ARCHITECTURE, README's figures and the work log.*

## 완료 조건 / Completion criteria

* 두 컴파일러 모두에서 벤치마크의 MIPS가 측정 가능하게 오르고, 수치를 분석에 적는다.
* 모든 대조 fuzz, trace 묶음, 견고성 하네스, 단위 테스트가 통과한다(결과 불변).
* 다섯 호스트 CI와 libFuzzer 작업이 녹색이다.

*MIPS rises measurably under both compilers, with the figures in the analysis; every comparison fuzz, trace corpus, the robustness harness and the unit tests pass (results unchanged); CI is green on the five hosts and the libFuzzer job.*
