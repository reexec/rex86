# #46 작업 로그 : 번역 차등의 캠페인 통합과 문서 / #46 work log : the translation differential in the campaign, and documents

이슈: [#46](https://github.com/reexec/rex86/issues/46) | 설계: [20261010-i046](../design/20261010-i046-translation-campaign.md) | 지시서: [20261010-i046](../work-orders/20261010-i046-translation-campaign.md) | 상위: [#42](../design/20261010-i042-phase3-translation.md)

## 2026-10-10

호스트: AMD Ryzen 5 5600X(Zen 3) 12 스레드, Linux, GCC 13, emsdk 3.1.74, Node 24.19. 브랜치는 `work/i043-ir-frontend`(#43~#45 위에 쌓음).

- **발견**(코드 읽기, 설계 결정 1): 단일 스텝 재생(`Cpu::Step`, 예산 1)에서는 명령이 여럿인 블록이 번역으로 돌지 않는다. 그래서 #44, #45의 번역 강제 빌드에서도 trace 묶음의 단일 스텝 사례는 인터프리터로 돌았다. 번역을 탄 것은 `Run(budget)` 사례, 단위 테스트, 견고성, 벤치마크다. 그 작업 로그의 "ctest 전부가 번역 경로로"라는 표현은 이 범위로 읽어야 한다.
- **구현**:
  - `TranslationOptions::max_block_instructions`(기본 64)를 실행기와 프런트엔드에 이었다.
  - `rex86_int_fuzz --translate`: `SetDefaultTranslation({kEvaluator, 0, 블록 길이 1})`.
  - 단위 테스트: 블록 길이 1에서 `Step` 300번이 인터프리터와 같고 번역 단계가 250을 넘는지, 기본 길이에서 번역 단계가 그 절반보다 적은지. 처음 쓴 "기본 길이에서는 블록 실행 0"은 틀린 가정이었다. 한 단계씩 실행하면 `jnz`처럼 명령 하나로 끝나는 블록의 머리에 서고, 그 블록은 예산 1로도 돈다.
  - 캠페인: `int-i386` matrix의 `translate` 행, `irdiff-x64`, `wasm-translate`(IR 차등과 번역 강제 `WASM` 견고성). 기본 시드 80M, 85M, 90M, 95M을 #37 설계의 표에 이었다.
  - 문서: README(3단계 행, 현재 수치), 분석 `integer-host-comparison.md` 3.1절, 캠페인 가이드의 작업 표, 정수 fuzz 가이드의 `--translate`, ARCHITECTURE.
- **검증**:
  - `--translate`가 실제로 번역하는지(변이 시험): 프런트엔드 AF 결함에서 `--translate` 2만 건 불일치 584, 없이는 0.
  - 긴 실행: i386 Release `--translate` 8 × 125만 건(시드 4600000~4600007), 불일치 0, `vendor_deviations` 100(Zen 3 BOUND 비율), shard당 약 93초.
  - 번역 강제 `WASM` 빌드의 견고성 1,000건이 Node에서 5.5초(전체 규모를 shard당 5만 건으로 정한 근거).
  - 단위 checks 1,992, failures 0(x86-64 Debug).

*Host: AMD Ryzen 5 5600X (Zen 3), 12 threads, Linux, GCC 13, emsdk 3.1.74, Node 24.19; branch `work/i043-ir-frontend` (stacked on #43-#45). Found by reading the code (design decision 1): under single-step replay (`Cpu::Step`, a budget of 1) blocks of several instructions never run translated, so the forced-translation builds of #44 and #45 ran the trace corpus's single-step cases on the interpreter, translation covering `Run(budget)` cases, the unit tests, robustness and the benchmark; those logs' "all of ctest through translation" is to be read within that scope. Implemented: `TranslationOptions::max_block_instructions` (default 64) wired into the runtime and frontend; `rex86_int_fuzz --translate` calling `SetDefaultTranslation({kEvaluator, 0, block length 1})`; unit tests checking that 300 `Step`s at block length 1 equal the interpreter with more than 250 translated steps and that the default length translates fewer than half as many, the first-written "no block runs at the default length" being a wrong assumption since stepping lands on block heads such as the `jnz`, a one-instruction block that runs within a budget of 1; in the campaign, the `translate` row of the `int-i386` matrix, `irdiff-x64` and `wasm-translate` (the IR differential and forced-`WASM` robustness), base seeds 80M, 85M, 90M and 95M appended to #37's design table; documents: README (the phase 3 row, current figures), section 3.1 of the analysis `integer-host-comparison.md`, the campaign guide's job table, `--translate` in the integer fuzz guide, ARCHITECTURE. Verification: `--translate` really translates (mutation test: with a frontend AF defect 584 mismatches in 20,000 cases with it, none without); a long i386 Release `--translate` run of 8 × 1.25M (seeds 4600000-4600007) with zero mismatches and 100 vendor deviations (Zen 3's BOUND rate), about 93 s per shard; forced-`WASM` robustness at 1,000 cases in 5.5 s on Node, the basis for 50,000 per shard at full scale; 1,992 unit checks, zero failures (x86-64 Debug).*

### 이어서 할 일 / Next

1. 커밋, push. 캠페인 파일이 바뀌었으므로 브랜치 push가 smoke 캠페인을 돌린다. 새 작업 셋을 포함해 녹색인지 본다.
2. 다섯 호스트 CI 확인.
3. 3단계(#42~#46)의 머지는 사용자 요청 때.

*Next: 1 commit and push; the campaign files changed, so the branch push runs the smoke campaign, to be checked green with the three new jobs; 2 check CI on the five hosts; 3 merging phase 3 (#42-#46) on the user's request.*
