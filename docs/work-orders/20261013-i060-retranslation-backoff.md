# #60 작업 지시 : 자기 수정 코드의 재번역 억제와 모듈 수 대 처리량 / #60 work order : damping re-translation under self-modifying code, and module count against throughput

이슈: [#60](https://github.com/reexec/rex86/issues/60) | 설계: [20261013-i060](../design/20261013-i060-retranslation-backoff.md) | 로그: [20261013-i060](../work-logs/20261013-i060-retranslation-backoff.md)

## 작업 항목 / Tasks

1. `rex86_bench`에 `smc` 커널과 워크로드를 더한다. 쓸 수 있는 코드 페이지, 모델, 단위 테스트의 워크로드 수(설계 결정 1).
2. `Translator`: 세는 동안 머리 페이지 세대가 바뀌면 다시 세기(`count_generation`, 결정 2), `evictions`를 `backoff`로 바꾸고 코드 변경에도 올리기, `invalidated_count`(결정 3).
3. 공개 계약 `TranslationStats::blocks_invalidated`, `Cpu::translation_stats`, `rex86_bench`의 출력.
4. 단위 테스트: 새 억제 시험, 바뀌는 기존 시험.
5. 측정: 억제를 끈 빌드와 번갈아(네이티브 평가기, Node wasm). 묶음 크기별 모듈 수와 처리량(Node, Chromium, Firefox)(결정 4).
6. 문서: 분석 `interpreter-performance.md` 또는 `browser-wasm.md`의 해당 절, 가이드 `benchmark.md`, `ARCHITECTURE.md`, 작업 로그.

*1 the `smc` kernel and workload in `rex86_bench`: a writable code page, the model, the unit test's workload count (design decision 1); 2 in `Translator`: counting over when the head page's generation changes while counting (`count_generation`, decision 2), `evictions` renamed `backoff` and raised by code changes too, `invalidated_count` (decision 3); 3 the public contract's `TranslationStats::blocks_invalidated`, `Cpu::translation_stats` and `rex86_bench`'s output; 4 unit tests: the new backoff tests and the existing ones that change; 5 measurement alternately against a build with the damping off (native evaluator, Node wasm), and module count against throughput by batch size (Node, Chromium, Firefox) (decision 4); 6 documents: the relevant sections of the analyses `interpreter-performance.md` or `browser-wasm.md`, the guide `benchmark.md`, `ARCHITECTURE.md` and the work log.*

## 완료 조건 / Completion criteria

* 단위 테스트와 ctest가 x86-64, 번역 강제(평가기), wasm32에서 통과하고 CI가 녹색이다.
* `smc`에서 억제를 켠 빌드의 번역 수가 줄고, 처리량이 억제를 끈 빌드보다 낮지 않다. 다른 워크로드는 측정 변동 안에서 같다.

*Unit tests and ctest pass on x86-64, forced translation (evaluator) and wasm32, with CI green; on `smc` the damped build translates fewer blocks and runs no slower than the undamped one, other workloads staying the same within measurement variation.*
