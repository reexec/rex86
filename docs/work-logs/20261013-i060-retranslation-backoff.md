# #60 작업 로그 : 자기 수정 코드의 재번역 억제와 모듈 수 대 처리량 / #60 work log : damping re-translation under self-modifying code, and module count against throughput

이슈: [#60](https://github.com/reexec/rex86/issues/60) | 설계: [20261013-i060](../design/20261013-i060-retranslation-backoff.md) | 지시서: [20261013-i060](../work-orders/20261013-i060-retranslation-backoff.md)

## 2026-10-13

호스트 F: Intel Core i5-7200U(Kaby Lake), Windows 10의 WSL2 Ubuntu 24.04. GCC 13.3, emsdk 6.0.8(Node 24.19.0), Playwright의 headless Chromium 153과 Firefox. 브랜치는 #59의 `work/i059-small-follow-ups`다(작업 브랜치에서는 새 이슈도 그 브랜치에서 잇는다는 규칙).

*Host F: an Intel Core i5-7200U (Kaby Lake), WSL2 Ubuntu 24.04 under Windows 10, GCC 13.3, emsdk 6.0.8 (Node 24.19.0), Playwright's headless Chromium 153 and Firefox; on #59's branch `work/i059-small-follow-ups`, by the rule that a task branch carries new issues too.*

### `smc` 워크로드(설계 결정 1)

- `workloads.cpp`: `EmitSmc`(페이지 정렬, rehearsal로 주소를 얻음), `EmitSmcCode`, `SmcModel`, `PadToPage`. `Image::smc_page`, `LoadImage`가 그 페이지를 `kPageReadWriteExecute`로 둔다. `bench_test`의 워크로드 수는 8이다.
- 억제 전 기준(단발, 네이티브 Release): 인터프리터 1.81 MIPS, 평가기 1.31 MIPS와 블록 118,494개 번역(바퀴마다 약 244번). Node: 인터프리터 1.51, wasm 1.38(번역 2개, 대기열에서 버려진 블록을 계속 다시 만듦).

*`workloads.cpp` gains `EmitSmc` (page-aligned, addresses from a rehearsal), `EmitSmcCode`, `SmcModel` and `PadToPage`; `Image::smc_page`, set `kPageReadWriteExecute` by `LoadImage`; `bench_test` counts 8 workloads. Baseline before the damping (single runs, native Release): interpreter 1.81 MIPS, evaluator 1.31 MIPS with 118,494 blocks translated (some 244 a lap); Node: interpreter 1.51, wasm 1.38 (2 translated, queued blocks dropped and formed again and again).*

### 억제(결정 2, 3)

- `runtime.h/.cpp`: `count_generation`, `evictions` → `backoff`, `kMaxEvictionShift` → `kMaxBackoffShift`, `BackOff`, `invalidated_`. `Lookup`은 세대를 비교한 뒤 진입 수를 본다. 처음에는 세대 비교를 `count < needed` 안에 두었는데, 이미 문턱에 닿은 블록은 비교 없이 번역되었다. 단위 테스트가 잡았고, 비교를 앞으로 옮겼다.
- 번역 불가에서 다시 시도할 때, 설치 실패(`Refuse`, `kFailed`)일 때도 backoff를 남긴다. 그 전에는 지웠다.
- 공개 계약 `TranslationStats::blocks_invalidated`, `Cpu::translation_stats`, `rex86_bench`의 `modules_submitted=` 줄.
- 단위 테스트: `InvalidationBackoffTests`(2배와 4배, 세는 중 페이지가 바뀌면 다시 세기, 64배 상한)를 더했다. `StaleQueueTests`, `PendingBatchTests`, `BudgetTests`의 기대를 고쳤다. 무효가 된 블록이 같은 진입에서 다시 대기열에 들어가던 것이 이제 억제를 거친다.
- 확인: 네이티브 Release 단위 테스트 checks 2,301, failures 0. 번역 강제(평가기) Release ctest 9/9.

*`runtime.h/.cpp` gain `count_generation`, `evictions` becomes `backoff`, `kMaxEvictionShift` becomes `kMaxBackoffShift`, plus `BackOff` and `invalidated_`; `Lookup` compares generations before looking at the count. The comparison first sat inside `count < needed`, letting a block already at the threshold translate unchecked; the unit test caught it and the comparison moved ahead. Retrying an untranslatable block and a failed installation (`Refuse`, `kFailed`) keep the backoff, which they used to clear. The public contract's `TranslationStats::blocks_invalidated`, `Cpu::translation_stats`, and `rex86_bench`'s `modules_submitted=` line. Unit tests: `InvalidationBackoffTests` added (2 and 4 times, counting over when the page changes while counting, the 64-fold cap); `StaleQueueTests`, `PendingBatchTests` and `BudgetTests` get new expectations, a stale block that used to queue again on the same entry now backing off. Checked: native Release unit tests, 2,301 checks, 0 failures; forced translation (evaluator) Release ctest 9/9.*

### A/B 측정

억제를 끈 사본(`BackOff` 호출 둘과 세대 비교를 끔, 축출 억제는 그대로)과 번갈아 세 번 돌렸다. MIPS 범위다.

| 엔진 / engine | 워크로드 | 억제 켬 / damped | 억제 끔 / undamped |
|---|---|---|---|
| 평가기 / evaluator | smc | 2.57–2.63 (번역 2) | 2.40–2.44 (번역 118,494, 무효 118,492) |
| 평가기 / evaluator | alu | 29.2–31.3 | 29.4–32.1 |
| 평가기 / evaluator | mixed | 26.2–26.9 | 27.0–27.7 |
| 평가기 / evaluator | blocks | 6.22–6.26 | 6.09–6.13 |
| Node wasm | smc | 1.98–2.10 | 2.02–2.05 (대기열 무효 1,866) |
| Node wasm | alu | 84.8–86.8 | 78.6–85.3 |
| Node wasm | mixed | 26.5–27.6 | 25.6–27.0 |
| Node wasm | blocks | 4.19–4.48 | 4.28–4.51 |
| 인터프리터 / interpreter | smc | 2.45–2.68 | |

- **확인됨**: 평가기에서 `smc`의 번역이 118,494개에서 2개로 줄었다. 처리량은 인터프리터 수준으로 올랐다(약 8%).
- **확인됨**: wasm에서는 억제를 끄더라도 무효가 된 블록이 대기열에서 버려져 모듈이 생기지 않았다. 그래서 처리량 차이가 변동 안이다. 억제가 아끼는 것은 블록 형성과 최적화다.
- **확인됨**: 다른 워크로드는 변동 안에서 같다. 평가기 `mixed`가 1~3% 낮게 나왔지만 범위가 거의 맞닿아 있다.
- **관찰**: `smc`는 인터프리터부터 느리다(`alu`의 6분의 1 수준). 페이지 단위 디코드 캐시 무효화 탓이다. 설계의 후속 후보로 적었다.

*Run three times alternately against a copy with the damping off (both `BackOff` calls and the generation comparison off, eviction backoff kept), MIPS ranges as in the table. Confirmed: on the evaluator `smc` translations fell from 118,494 to 2 and throughput rose to the interpreter's level (some 8%); on wasm even the undamped build dropped stale blocks in the queue before any module, so throughput differs within the variation, the damping saving block forming and optimization; other workloads match within the variation, the evaluator's `mixed` 1 to 3% lower with ranges nearly touching. Observed: `smc` is slow on the interpreter already (a sixth of `alu`), from page-granular decode-cache invalidation, recorded as a follow-up candidate in the design.*

### 모듈 수와 처리량(결정 4)

- 브라우저: `REX86_BROWSER` 빌드를 scratch의 Playwright 스크립트로 돌렸다. 설정마다 새 브라우저를 띄우고 `worker.js?tool=rex86_bench&args=...`를 쓴다. Firefox는 Playwright로 받고, 빠진 NSS 라이브러리는 #59와 같이 `LD_LIBRARY_PATH`로 붙였다.
- 결과는 [분석](../analysis/browser-wasm.md) 3.2절에 적었다. 64가 16보다 빠르다. 설치 뒤 프레임도 약 14% 빠르다. 첫 프레임은 늦다. 예산을 넘는 작업 집합(묶음 1, 4)은 인터프리터보다 느리다.
- 기본값은 바꾸지 않았다(설계 결정 4).

*Browsers: the `REX86_BROWSER` build ran through a scratch Playwright script, a fresh browser per setting, using `worker.js?tool=rex86_bench&args=...`; Firefox came from Playwright, its missing NSS libraries supplied through `LD_LIBRARY_PATH` as in #59. Results are in section 3.2 of the analysis: 64 beats 16, frames after installation some 14% faster too, the first frame later, and a working set beyond the budget (batch sizes 1 and 4) slower than the interpreter. The defaults stay (design decision 4).*

### 다음 / Next

1. CI(번역 강제 wasm, Clang, 새니타이저)를 확인한다.
2. 머지는 사용자 요청을 기다린다. #59와 함께 머지된다.
3. 기본 묶음 크기의 재검토는 소비자 게스트 측정 뒤로 남긴다. 인터프리터의 페이지 단위 무효화는 후속 후보다.

*1 Check CI (forced wasm translation, Clang, sanitizers). 2 The merge waits for the user's request, together with #59. 3 Reconsidering the default batch size waits for consumer guest measurements; the interpreter's per-page invalidation is a follow-up candidate.*
