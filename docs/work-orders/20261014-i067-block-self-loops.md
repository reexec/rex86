# #67 작업 지시 : 자기 고리 블록을 생성 코드 안에서 반복 / #67 work order : self-looping blocks run in place

이슈: [#67](https://github.com/reexec/rex86/issues/67) | 설계: [20261014-i067](../design/20261014-i067-block-self-loops.md) | 로그: [20261014-i067](../work-logs/20261014-i067-block-self-loops.md)

## 작업 항목 / Tasks

1. 블록 전환을 센다(자기 시작, 다른 블록). 실험용 사본에서만 한다(설계의 측정).
2. `module_writer`: `loop`, `br`, `i32.le_u`.
3. `RunLimits`(`Backend::Run`, `Translator::Run`), `ExitResult::loops`, `Cpu`의 안쪽 고리가 넘기는 제한(`kMaxStepsInPlace` 2^30)과 `block_runs`(결정 2).
4. `codegen`: `RunContext`(`CheckContext`를 넓힘), `LoopsToStart`, `LoopBack`, `ExitCode`(결정 1). `WasmBackend::Run`이 문맥을 채운다. IR 차등은 한 번만 돌린다.
5. `WasmLoopTests`(예산 조각, 주의 신호).
6. 측정과 문서: 설계, 작업 로그, 분석 `interpreter-performance.md` 2.7절, ARCHITECTURE.

*1 Count block transitions (to the own start, to other blocks), in a scratch copy only (the design's measurement). 2 `module_writer`: `loop`, `br`, `i32.le_u`. 3 `RunLimits` (`Backend::Run`, `Translator::Run`), `ExitResult::loops`, and the limits the `Cpu`'s inner loop passes (`kMaxStepsInPlace`, 2^30) with `block_runs` (decision 2). 4 `codegen`: `RunContext` (grown from `CheckContext`), `LoopsToStart`, `LoopBack`, `ExitCode` (decision 1); `WasmBackend::Run` fills the context and the IR differential runs one pass. 5 `WasmLoopTests` (budget slices, attention). 6 Measurement and documents: the design, the work log, section 2.7 of the analysis `interpreter-performance.md`, and ARCHITECTURE.*

## 완료 조건 / Completion criteria

* 단위 테스트와 ctest가 네이티브, wasm32, 번역 강제 `WASM`에서 통과하고 CI가 녹색이다.
* alu와 memory가 v0.0.32보다 빠르고, 나머지는 느려지지 않는다.

*Unit tests and ctest pass natively, on wasm32 and in forced `WASM`, with CI green; alu and memory run faster than v0.0.32 and the rest no slower.*
