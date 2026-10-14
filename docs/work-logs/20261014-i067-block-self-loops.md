# #67 작업 로그 : 자기 고리 블록을 생성 코드 안에서 반복 / #67 work log : self-looping blocks run in place

이슈: [#67](https://github.com/reexec/rex86/issues/67) | 설계: [20261014-i067](../design/20261014-i067-block-self-loops.md) | 지시서: [20261014-i067](../work-orders/20261014-i067-block-self-loops.md)

## 2026-10-14

호스트 F: Intel Core i5-7200U, Windows 10의 WSL2 Ubuntu 24.04, GCC 13.3, emsdk 6.0.8(Node 24.19.0). 브랜치는 `work/i067-block-self-loops`(main `b954127`, v0.0.32에서)다.

*Host F: an Intel Core i5-7200U, WSL2 Ubuntu 24.04 under Windows 10, GCC 13.3, emsdk 6.0.8 (Node 24.19.0); branch `work/i067-block-self-loops` from main `b954127` (v0.0.32).*

### 측정

- 실험용 사본의 `Cpu::RunUntilStop` 안쪽 고리에 계수기를 넣었다(커밋하지 않음). 네이티브 평가기로 5프레임씩 돌렸다. 자기 시작으로 돌아가는 비율은 alu 99.9%(555,646 중 555,097), memory 99.9%(630,408 중 629,928), call 0%, mixed 55%(158,983 중 87,624), blocks 0%다.
- 그래서 이 작업은 자기 고리만 다루고, 다른 블록으로의 연결은 측정 뒤로 미뤘다(설계 결정 3).

*A counter in the inner loop of `Cpu::RunUntilStop` in a scratch copy (not committed), five frames on the native evaluator: the share going back to the block's own start is 99.9% on alu (555,097 of 555,646), 99.9% on memory (629,928 of 630,408), 0% on call, 55% on mixed (87,624 of 158,983) and 0% on blocks. So this work takes self-loops only, leaving linking to other blocks until after measuring (design decision 3).*

### 구현

- `module_writer`: `Loop`, `Br`, `kI32LeU`.
- `runtime`: `RunLimits`, `Backend::Run`과 `Translator::Run`이 그것을 받는다. 평가기 백엔드와 시험용 백엔드는 무시한다. `ExitResult::loops`.
- `cpu.cpp`: 안쪽 고리가 `min(남은 예산, 2^30)`과 `attention`의 주소를 넘기고, `block_runs`에 `1 + loops`를 더한다.
- `codegen`: `RunContext`(`flags`, `size`, `max_steps`, `attention`, `loops`), 자기 고리 블록의 `loop`, `LoopBack`, `ExitCode`, 임시 지역 넷째(`done`).
- `irdiff`: `Backend::Run`을 직접 부르는 곳이 있어 첫 빌드가 깨졌다. 한 번만 돌도록(`max_steps` = 블록 명령 수) 고쳤다. 평가기와 한 번씩 비교하는 도구이기 때문이다.
- `WasmLoopTests`: 고리 프로그램(`kLoop`)을 예산 1, 3, 7, 50, 100만 단계로 잘라 매번 인터프리터와 비교한다. 대기 인터럽트(IF 꺼짐)로 주의 신호가 켜진 채로도 비교한다.

*`module_writer`: `Loop`, `Br`, `kI32LeU`. `runtime`: `RunLimits`, taken by `Backend::Run` and `Translator::Run` and ignored by the evaluator and test backends; `ExitResult::loops`. `cpu.cpp`: the inner loop passes `min(remaining budget, 2^30)` and the address of `attention`, and adds `1 + loops` to `block_runs`. `codegen`: `RunContext` (`flags`, `size`, `max_steps`, `attention`, `loops`), a `loop` around self-looping blocks, `LoopBack`, `ExitCode` and a fourth temporary local (`done`). `irdiff` called `Backend::Run` directly and broke the first build; it now runs one pass (`max_steps` = the block's instruction count), being a tool that compares one pass with the evaluator. `WasmLoopTests` runs the loop program (`kLoop`) with the budget cut into slices of 1, 3, 7, 50 and a million steps, comparing with the interpreter every time, and again with attention raised by a pending interrupt while IF is clear.*

### 검증과 결과

- 시험(로컬):
  - wasm 단위 테스트 checks 2,381, 실패 0.
  - wasm32 ctest 8/8, 번역 강제 `WASM` ctest 8/8, 네이티브 Release ctest 9/9.
- 한 번 trace 묶음 시험이 세 구성 모두에서 실패했다. 실험 스크립트가 작업 사본을 WSL로 옮기며 모든 파일의 CR 바이트를 지웠는데, 이진 파일인 `.rxt` trace도 망가뜨렸다. 원본을 다시 복사하자 통과했다. 코드와 무관하다.
- 측정(v0.0.32와 번갈아 일곱 번 최고값, wasm, 부하 평균 4~5): alu 59.9 → 97.8(+63%), memory 44.2 → 60.8(+38%). call, blocks, mixed는 변동 안이다(설계의 표).

*Local tests: wasm unit tests 2,381 checks, 0 failures; wasm32 ctest 8/8, forced `WASM` ctest 8/8, native Release ctest 9/9. Once the trace corpus test failed in all three configurations: the scratch script stripped CR bytes from every file it copied into WSL, binary `.rxt` traces included; copying the originals again made it pass, nothing to do with the code. Measurement (the best of seven interleaved with v0.0.32, wasm, load average 4 to 5): alu 59.9 to 97.8 (+63%), memory 44.2 to 60.8 (+38%), and call, blocks and mixed within the variation (the design's table).*

### 다음 / Next

1. (끝남) CI가 모두 녹색이다([실행 38529332901](https://github.com/reexec/rex86/actions/runs/38529332901)). 번역 강제 `WASM`과 브라우저 셋이 포함된다.
2. 다른 블록으로의 연결(설계 결정 3): 정적 목적지(CALL, JMP)와 동적 목적지(RET). tail call 지원과 스택 깊이를 따져 범위를 정한다.

*1 (done) CI green (run 38529332901), forced `WASM` and the three browsers included. 2 Linking to other blocks (design decision 3): static targets (CALL, JMP) and dynamic ones (RET), scoped by tail-call support and stack depth.*
