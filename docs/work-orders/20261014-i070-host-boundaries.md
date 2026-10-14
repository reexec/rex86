# #70 작업 지시 : 호스트 경계 명령 / #70 work order : host boundary instructions

이슈: [#70](https://github.com/reexec/rex86/issues/70) | 설계: [20261014-i070](../design/20261014-i070-host-boundaries.md) | 로그: [20261014-i070](../work-logs/20261014-i070-host-boundaries.md)

1. `environment.h`: `StopReason::kHostBoundary`, `Features::host_boundaries`, `Environment::IsHostBoundary`.
2. `decoder.h`: `DecodedInstruction::host_boundary`. `interpreter.h/.cpp`: 검사와 건너뛰기 입력. `cpu.h/.cpp`: `SkipHostBoundaryOnce`, 번역 제한에 환경 전달.
3. `translate/runtime`, `ir/frontend`: 경계에서 블록 종료.
4. `StopReason`를 switch하는 도구(bench, probe)에 새 값.
5. `host_boundary_test.cpp`. Windows x86 MSVC, WSL Linux x64 GCC(`-Werror`).

*(1) The contract in `environment.h`. (2) The cached answer, the check and the skip input, `SkipHostBoundaryOnce` and the environment passed through the translator limits. (3) Blocks end at boundaries. (4) The new value in the tools that switch over `StopReason`. (5) The tests, on Windows x86 MSVC and WSL Linux x64 GCC with `-Werror`.*
