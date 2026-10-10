# #46 작업 지시 : 번역 차등의 캠페인 통합과 문서 / #46 work order : the translation differential in the campaign, and documents

이슈: [#46](https://github.com/reexec/rex86/issues/46) | 설계: [20261010-i046](../design/20261010-i046-translation-campaign.md) | 로그: [20261010-i046](../work-logs/20261010-i046-translation-campaign.md)

## 작업 항목 / Tasks

1. `TranslationOptions::max_block_instructions`를 실행기와 프런트엔드에 잇는다(설계 결정 1). 단위 테스트.
2. `rex86_int_fuzz --translate`(결정 2). 로컬 i386 긴 실행.
3. `fuzz-campaign.yml`: `irdiff-x64`, `irdiff-wasm`, `robust-wasm`, `int-i386`의 번역 행(결정 3). 브랜치 push의 smoke로 확인.
4. 문서(결정 4)와 작업 로그.

*1 wire `TranslationOptions::max_block_instructions` into the runtime and frontend (design decision 1), with unit tests; 2 `rex86_int_fuzz --translate` (decision 2), with a long local i386 run; 3 `fuzz-campaign.yml` gains `irdiff-x64`, `irdiff-wasm`, `robust-wasm` and the translated `int-i386` row (decision 3), checked by the branch push's smoke; 4 documents (decision 4) and the work log.*

## 완료 조건 / Completion criteria

* 브랜치 push의 smoke 캠페인이 새 작업을 포함해 모두 녹색이다.
* `rex86_int_fuzz --translate`의 로컬 긴 실행이 불일치 0이다.
* README의 3단계 행과 현재 수치가 갱신된다.
* 다섯 호스트 CI와 libFuzzer 작업이 녹색이다.

*The branch push's smoke campaign is green, new jobs included; a long local run of `rex86_int_fuzz --translate` has zero mismatches; README's phase 3 row and current figures are updated; CI is green on the five hosts and the libFuzzer job.*
