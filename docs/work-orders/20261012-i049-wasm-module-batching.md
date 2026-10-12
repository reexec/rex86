# #49 작업 지시 : wasm 묶음 설치와 모듈 수 예산 / #49 work order : batched wasm installation and a module count budget

이슈: [#49](https://github.com/reexec/rex86/issues/49) | 설계: [20261012-i049](../design/20261012-i049-wasm-module-batching.md) | 로그: [20261012-i049](../work-logs/20261012-i049-wasm-module-batching.md)

## 작업 항목 / Tasks

1. 공개 계약: `TranslationOptions`의 `blocks_per_module`, `batch_wait`, `max_modules`, `TranslationStats`의 `modules_submitted`, `modules_evicted`(설계 결정 4).
2. `Backend::Compile`을 번역 배열을 받게 바꾸고 `BatchCapacity`를 더한다. 평가기는 1, wasm 백엔드는 옵션값(결정 2).
3. `Translator`: `kQueued` 상태, 묶음 대기열과 제출 규칙, 묶음 티켓, 모듈 기록, 예산 축출(결정 2, 3).
4. 번역 강제 `WASM` 초기화에 `blocks_per_module = 1`.
5. 실행기 단위 테스트(시험용 백엔드), wasm32 단위 테스트(결정 6).
6. IR 차등의 wasm 비교 묶음(`--wasm-batch`).
7. 벤치마크: 워크로드 `blocks`, 모듈 보고, 옵션 셋(결정 5). 웹 어댑터에 실패 수.
8. 측정으로 `blocks_per_module`과 `batch_wait`의 기본값을 정한다(Node, Chromium, Firefox).
9. 브라우저 모음: IR 차등 2만 건, 테스트 사이 대기 재측정, 모듈 한도 탐침 `module_limit.html`과 CI WebKit 한 번 실행.
10. 문서: ARCHITECTURE, 분석(`browser-wasm.md` 3절, 성능), README 상태 표, 가이드, 작업 로그.

*1 The public contract: `TranslationOptions`'s `blocks_per_module`, `batch_wait`, `max_modules` and `TranslationStats`'s `modules_submitted`, `modules_evicted` (design decision 4); 2 `Backend::Compile` takes an array of translations and `BatchCapacity` is added, 1 for the evaluator and the option for the wasm backend (decision 2); 3 `Translator`: the `kQueued` state, the batch queue and its submission rules, batch tickets, module records, eviction at the budget (decisions 2 and 3); 4 `blocks_per_module = 1` in the forced `WASM` initializer; 5 runtime unit tests (a test backend) and wasm32 unit tests (decision 6); 6 batched wasm comparisons in the IR differential (`--wasm-batch`); 7 the benchmark: the `blocks` workload, module reporting, the three options (decision 5), and a failure count in the web adapter; 8 choose the defaults of `blocks_per_module` and `batch_wait` by measurement (Node, Chromium, Firefox); 9 the browser suite: 20,000 IR differential cases, the wait between tests measured again, the module limit probe `module_limit.html` with one CI WebKit run; 10 documents: ARCHITECTURE, the analysis (`browser-wasm.md` section 3, performance), README's status table, the guide, the work log.*

## 완료 조건 / Completion criteria

* Firefox에서 `blocks` 워크로드의 설치 거절이 묶음 기본값으로 0이다.
* 브라우저 모음이 Chromium과 Firefox에서 IR 차등 2만 건으로 통과하고, CI의 `browsers`가 세 브라우저에서 녹색이다.
* ctest 전체 통과: 네이티브 x86-64, wasm32, 번역 강제 `WASM`. 다섯 호스트 CI가 녹색이다.

*Firefox's refusals on the `blocks` workload are 0 with the batching defaults; the browser suite passes on Chromium and Firefox with 20,000 IR differential cases and CI's `browsers` is green on the three browsers; the whole ctest passes on native x86-64, wasm32 and forced `WASM`, with CI green on the five hosts.*
