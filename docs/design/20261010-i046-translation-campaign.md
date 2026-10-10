# #46 설계 : 번역 차등의 캠페인 통합과 문서 / #46 design : the translation differential in the campaign, and documents

이슈: [#46](https://github.com/reexec/rex86/issues/46) | 상위 설계: [#42](20261010-i042-phase3-translation.md) 결정 9, 11 | 근거: [#37 설계](20261010-i037-tag-fuzz-campaign.md), [#43](20261010-i043-ir-frontend.md), [#44](20261010-i044-translation-runtime.md), [#45](20261010-i045-wasm-backend.md) | 지시서: [20261010-i046](../work-orders/20261010-i046-translation-campaign.md) | 로그: [20261010-i046](../work-logs/20261010-i046-translation-campaign.md)

3단계의 마지막 하위 이슈다. 번역 차등을 릴리스 캠페인에 넣고, 호스트 대조 fuzz를 번역 엔진으로도 돌리며, README와 분석 문서에 3단계의 상태를 적는다.

*The last sub-issue of phase 3: the translation differential joins the release campaign, the host-comparison fuzz also runs through translation, and README and the analyses record phase 3's state.*

## 결정 1: 블록 길이 상한을 설정으로 / Decision 1: the block length cap as a setting

단일 스텝 재생은 `Cpu::Step()`, 곧 예산 1이다. 번역 블록은 예산이 블록 전체를 덮을 때만 돌므로(#44 결정 1), 명령이 여럿인 블록은 단일 스텝에서 번역으로 돌지 않는다. **확인됨(코드 읽기)**: 그래서 번역 강제 빌드(#44, #45)에서도 trace 묶음의 단일 스텝 사례는 인터프리터로 돌았다. 번역을 탄 것은 `Run(budget)` 사례와 단위 테스트, 견고성, 벤치마크다.

* `TranslationOptions::max_block_instructions`(기본 64)를 더한다. 1이면 명령마다 블록 하나라 단일 스텝도 번역으로 돈다.
* 번역 강제 빌드의 기본값은 그대로 64다(실제 쓰임과 같은 블록). 단일 스텝 사례를 번역으로 보는 일은 결정 2의 fuzz가 맡는다.

*Single-step replay is `Cpu::Step()`, a budget of 1, and translated blocks run only when the budget covers them whole (#44 decision 1), so blocks of several instructions never run translated under single-stepping. Confirmed by reading the code: the forced-translation builds (#44, #45) therefore ran the trace corpus's single-step cases on the interpreter, translation covering the `Run(budget)` cases, unit tests, robustness and the benchmark. `TranslationOptions::max_block_instructions` (default 64) is added; at 1 every instruction is its own block and single steps run translated too. Forced builds keep 64 (blocks as real use forms them); seeing single-step cases translated is decision 2's fuzz's job.*

## 결정 2: 호스트 대조 fuzz의 번역 실행 / Decision 2: the host-comparison fuzz through translation

* `rex86_int_fuzz --translate`: 실행 전에 `SetDefaultTranslation({kEvaluator, 0, 블록 길이 1})`을 부른다. 기대값(호스트 CPU)은 같고, 코어 쪽 재생이 번역을 탄다. 제조사 차이 판정의 재생도 같은 설정이다.
* x87과 SIMD fuzz에는 더하지 않는다. 그 명령들은 번역 범위 밖이라 인터프리터로 돌 뿐이다.
* 번역은 인터프리터와 같아야 하고(I8, IR 차등), 인터프리터는 호스트 CPU와 같으므로 이것은 세 번째 확인이다. 하지만 인터프리터를 거치지 않고 하드웨어와 직접 비교한다는 점에서 따로 둔다.

*`rex86_int_fuzz --translate` calls `SetDefaultTranslation({kEvaluator, 0, block length 1})` before running: the expectations (the host CPU) are the same and the core's replay runs through translation, the vendor-deviation replays included. The x87 and SIMD fuzzes do not get it, their instructions being outside the coverage and simply interpreted. Translation must equal the interpreter (I8, the IR differential) and the interpreter equals the host CPU, so this is a third check, kept because it compares with hardware without going through the interpreter.*

## 결정 3: 캠페인 작업 / Decision 3: campaign jobs

| 작업 | 내용 | 전체 규모 | smoke | 기본 시드 |
|---|---|---|---|---|
| `irdiff-x64` | `rex86_irdiff` x86-64 Release(평가기) | 4 × 500만 | 4 × 2만 | 80,000,000 |
| `wasm-translate`의 IR 차등 | `rex86_irdiff` wasm32 Node(평가기와 wasm) | 4 × 50만 | 4 × 5,000 | 90,000,000 |
| `wasm-translate`의 견고성 | `rex86_robust` wasm32 번역 강제 `WASM`(I8이 wasm과 비교) | 4 × 5만 | 4 × 100 | 85,000,000 |
| `int-i386` 행 하나 더 | `rex86_int_fuzz --translate` | 2,000만 | 4 × 2만 | 95,000,000 |

* 기존 견고성 작업(Release, ASan, AArch64)은 #44부터 케이스마다 I8(평가기)을 돈다. 따로 더할 것이 없다.
* 기본 시드는 #37 결정 3의 표 아래에 이어 붙인다. 모두 10^8보다 작으므로 다음 릴리스의 구간과 겹치지 않는다.
* wasm 쪽 두 실행은 빌드를 공유하므로 작업 `wasm-translate` 하나에 둔다.
* 규모는 로컬 처리량에서 정했다(wasm IR 차등 25만 건에 53초, 번역 강제 `WASM` 견고성 1,000건에 5.5초). **추정**: 각 작업 30분 안. 첫 전체 실행에서 고친다.

*The jobs are in the table above. The existing robustness jobs (Release, ASan, AArch64) have run I8 (the evaluator) on every case since #44, so nothing is added for them. The base seeds continue #37 decision 3's table, all below 10^8, so they never meet the next release's range. The two wasm runs share a build and live in one job, `wasm-translate`. Scales come from local throughputs (53 s for 250,000 wasm IR differential cases, 5.5 s for 1,000 forced-`WASM` robustness cases; estimate: each job within 30 minutes), corrected at the first full run.*

## 결정 4: 문서 / Decision 4: documents

* README: 로드맵 3단계 행(완료와 남은 것), 현재 수치에 번역 엔진의 첫 기록, 목표 3과 8의 진행.
* 분석: `integer-host-comparison.md`에 번역 실행 결과, `robustness-fuzz.md`에 캠페인의 I8.
* 가이드: `fuzz-campaign.md`의 작업 표, 정수 fuzz 가이드의 `--translate`.
* ARCHITECTURE: 엔진 구조 표의 3단계 상태.

*README: the roadmap's phase 3 row (done and remaining), the translating engines' first record in the current figures, and progress on goals 3 and 8. Analyses: the translated run's result in `integer-host-comparison.md`, the campaign's I8 in `robustness-fuzz.md`. Guides: the job table in `fuzz-campaign.md` and `--translate` in the integer fuzz guide. ARCHITECTURE: phase 3's state in the engine structure table.*

## 소비자 영향 / Consumer impact

`TranslationOptions::max_block_instructions`가 더해진다(추가만, 기본값은 지금과 같은 64). 그 밖에는 테스트와 문서뿐이다.

*`TranslationOptions::max_block_instructions` is added (addition only, its default the current 64); the rest is tests and documents.*
