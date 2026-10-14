# #65 작업 지시 : 번역 경로 성능 / #65 work order : translation path performance

이슈: [#65](https://github.com/reexec/rex86/issues/65) | 설계: [20261014-i065](../design/20261014-i065-translation-dispatch.md) | 로그: [20261014-i065](../work-logs/20261014-i065-translation-dispatch.md)

## 작업 항목 / Tasks

1. 측정: wasm 번역 경로에서 블록 하나의 비용을 나눈다(Node CPU 프로파일, 블록 통계). 결과를 보고하고 개선 순서를 정한다(설계 결정 1).
2. 디스패치 빠른 경로: `Cpu::RunUntilStop`의 안쪽 고리, `Translator`의 조회 캐시, `ModesSupported`, `Generation`, 캐시 경로의 `Lookup`을 인라인으로(결정 2).
3. 인라인 메모리 검사: `Generator::InlineCheck`, `CheckContext`, `PageAttributeTable::data()`, `WasmCheckTests`(결정 3).
4. 측정: main과 번갈아(링크 최적화를 끈 프로파일, 일곱 번 중 최고값).
5. 문서: 설계, 작업 로그, 분석 `interpreter-performance.md`, ARCHITECTURE.

*1 Measure: split one block's cost on the wasm translation path (Node CPU profiles, block statistics), report, and settle the order of improvements (design decision 1). 2 A dispatch fast path: the inner loop of `Cpu::RunUntilStop`, the lookup cache in `Translator`, and `ModesSupported`, `Generation` and the cache path of `Lookup` made inline (decision 2). 3 Inline memory checks: `Generator::InlineCheck`, `CheckContext`, `PageAttributeTable::data()`, `WasmCheckTests` (decision 3). 4 Measure against main, interleaved (profiles with link-time optimization off, the best of seven). 5 Documents: the design, the work log, the analysis `interpreter-performance.md` and ARCHITECTURE.*

## 완료 조건 / Completion criteria

* 단위 테스트와 ctest가 네이티브, wasm32, 번역 강제(평가기, wasm)에서 통과하고 CI가 녹색이다.
* 범위 안의 워크로드(alu, memory, call)가 wasm에서 main보다 빠르고, 나머지는 느려지지 않는다.

*Unit tests and ctest pass natively, on wasm32 and in forced translation (evaluator, wasm), with CI green; the workloads inside the coverage (alu, memory, call) run faster than main on wasm and the rest no slower.*
