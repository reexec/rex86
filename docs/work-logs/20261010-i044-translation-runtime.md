# #44 작업 로그 : 번역 실행기 / #44 work log : the translation runtime

이슈: [#44](https://github.com/reexec/rex86/issues/44) | 설계: [20261010-i044](../design/20261010-i044-translation-runtime.md) | 지시서: [20261010-i044](../work-orders/20261010-i044-translation-runtime.md) | 상위: [#42](../design/20261010-i042-phase3-translation.md)

## 2026-10-10

호스트: AMD Ryzen 5 5600X(Zen 3) 12 스레드, Linux, GCC 13, Clang 18, emsdk 3.1.74. 브랜치는 #43과 같은 `work/i043-ir-frontend`다.

- **사용자 결정**: #44를 #43 위에 쌓는다. 새 브랜치는 `main`일 때만 만들고, 작업 브랜치에서는 새 이슈도 이어서 한다. AGENTS.md 브랜치 규칙에 이를 더했다(PR 본문에는 쌓인 이슈를 모두 `Closes #N`으로 적는다).
- **맥락 확인**: `Cpu::RunUntilStop`은 정지 요청, 게이트, 인터럽트 전달, 예산을 본 뒤 `interp::RunBlock`을 부른다. `RunBlock`은 분기를 넘어 계속 실행하므로, 번역을 붙이려면 블록 머리에서 루프로 돌아오는 길이 필요했다(설계 결정 2).
- **구현**:
  - 인터프리터: `StepResult::branched`(분기한 길에서만 씀), `BlockLimits::stop_after_branch`, `RunBlock`을 템플릿 두 판으로 나눔.
  - `src/translate/runtime`: `Translator`(캐시, 세대로 유효성, 문턱, 번역 불가 기억, 65,536 한도, `Flush`), 백엔드 계약, `EvaluatorBackend`(값 버퍼 재사용). 평가기에 바깥 버퍼를 받는 `Evaluate`를 더했다.
  - `Cpu`: `TranslationMode`, `TranslationOptions`, `SetTranslation`, `translation()`, `TranslationStats`와 `translation_stats()`, 디스패치, `kInterpret` 뒤 한 단계 강제, `RegisterGate`의 비움, `ActiveEngine`, `EngineMemoryBytes`. CMake `REX86_FORCE_TRANSLATION`.
  - **설계에 없던 추가**: `TranslationStats`. 번역 강제 빌드가 모든 테스트를 통과해도 번역이 실제로 일어났는지 밖에서 볼 수 없었기 때문이다. 벤치마크와 소비자의 진단 화면에도 쓴다. 설계 결정 4에 반영할 것(아래 이어서 할 일).
  - 견고성 하네스: `RunCase`가 번역 설정을 받고, `RunCaseTwice`가 세 번째 실행으로 I8을 확인한다. 비교용 `architectural_digest`(kTranslated 비트와 코드 페이지 쓰기 횟수 제외).
  - 벤치마크: `--engine evaluator`, 워크로드마다 `engine_bytes`와 번역 통계 줄.
  - 단위 테스트 `translate_runtime_test.cpp`: 루프(문턱 0, 4, 32), 예산 1, 2, 3, 7로 400번씩, 검사 실패 뒤 폴트, 번역된 루프 안에 나중에 등록한 게이트, 자기 수정 코드와 `InvalidateCode`, 정지 요청과 번역 끄기.
  - CI: `linux-x64-translate`(GCC Debug ASan/UBSan, 예열 0, 번역 강제).
  - wasm32 테스트 도구에 `-sALLOW_MEMORY_GROWTH=1`(아래).
- **구현 중의 실패와 원인**:
  - 번역 강제 빌드에서 `cpu_test`의 "`ActiveEngine`이 인터프리터"가 실패했다. 기대대로다. 테스트가 번역 설정에 따라 기대값을 고르게 했다.
  - 실행기 테스트 12건이 실패했다. 테스트 코드의 `jnz` 변위가 1씩 틀려(0xF5, 0xF3, 맞는 값은 0xF4) 명령 한가운데로 뛰었다. 이때도 번역과 인터프리터는 같은 결과를 냈다.
  - Clang이 테스트의 쓰지 않는 상수를 `-Werror`로 잡았다(GCC는 경고하지 않음).
  - **wasm32 번역 강제 빌드의 단위 테스트가 OOM**으로 멈췄다. 네이티브 최대 RSS는 일반 12.5 MB, 강제 12.7 MB로 거의 같다. 단위 테스트 자체가 Emscripten의 기본 16 MiB에 가까웠다(벤치마크 테스트가 4 MiB 게스트와 4.6 MB 디코드 캐시를 쓴다). 테스트 도구에만 메모리 증가를 켰다. 브라우저 소비자도 켜는 설정이다. 번역 메모리는 벤치마크 기준 블록 하나에 약 3 KB다.
- **변이 시험**:
  - 프런트엔드의 AF 결함: 견고성 3,000건 안에 I8 위반 여러 건.
  - `kInterpret` 뒤 강제 한 단계를 없앰: 견고성 하네스와 단위 테스트가 끝나지 않음(120초 제한에 걸림). 설계 결정 1의 근거가 확인됐다. 하네스에 시간 감시가 없다는 것은 분석 문서의 미확정에 적었다.
- **빌드와 ctest**(`-Werror`):

  | 구성 | 결과 |
  |---|---|
  | x86-64 Debug, Release, Clang Debug | 각 8/8 |
  | x86-64 번역 강제 Debug, 번역 강제 ASan/UBSan 예열 0 | 각 8/8 |
  | i386 번역 강제 Release | 9/9 |
  | wasm32, wasm32 번역 강제 | 각 6/6 |

  - 단위 checks 1,976, failures 0(일반, 강제).
- **견고성 I8**: 6 × 5만 건(시드 4400000부터) 위반 0, shard당 약 190초.
- **인터프리터 비용**(cachegrind, 이 기계에서 소스로 빌드한 valgrind 3.23.0, 수정 전 `main` `adc0452`와 비교, 단계당 호스트 명령):

  | 워크로드 | main | 이 작업 |
  |---|---|---|
  | alu | 491.28 | 472.28 |
  | memory | 478.70 | 459.70 |
  | call | 479.47 | 460.47 |
  | x87 | 1021.81 | 1001.10 |

  - 번역이 꺼진 경로는 늘지 않았고 약 19개(3.9%) 줄었다. `RunBlock`을 템플릿으로 나누며 배치가 바뀐 것으로 **추정**한다.
  - 이 기계는 `perf_event_paranoid`가 4라 하드웨어 카운터를 쓸 수 없고 valgrind도 없어, valgrind를 scratchpad에 빌드했다.
- **평가기 모드의 첫 처리량**(같은 기계, Release, 10프레임, 한 번씩, 기록만): alu 19.3 → 52.8 MIPS, memory 32.3 → 30.5, call 39.7 → 40.4, string 76.9 → 44.3, x87 17.3 → 8.4, mixed 63.2 → 47.7(인터프리터 → 평가기). 번역 통계로 보면 x87은 블록 2개만, string은 블록마다 인터프리터 탈출이다. 다루는 범위 밖의 명령이 많은 워크로드는 블록마다 인터프리터 한 단계와 조회를 더 내기 때문이다. **추정**: 범위가 넓어지면 평가기가 JIT 없는 호스트의 빠른 경로가 될 수 있다(#42 결정 1). 측정 방식(번갈아 여러 번)으로 다시 재야 한다.

*Host: AMD Ryzen 5 5600X (Zen 3), 12 threads, Linux, GCC 13, Clang 18, emsdk 3.1.74; the branch is #43's `work/i043-ir-frontend`. User decision: #44 stacks on #43; a new branch is made only from `main`, and a task branch carries new issues too, which AGENTS.md's branch rules now say (the PR body names every stacked issue as `Closes #N`). Context: `Cpu::RunUntilStop` checks stop requests, gates, interrupt delivery and the budget, then calls `interp::RunBlock`, which runs on across branches, so translation needed a way back to the loop at block heads (design decision 2). Implemented: in the interpreter `StepResult::branched` (written only on the branch path), `BlockLimits::stop_after_branch` and `RunBlock` split into two template versions; `src/translate/runtime` with the `Translator` (cache, generation validity, threshold, remembered untranslatable heads, 65,536 cap, `Flush`), the backend contract and an `EvaluatorBackend` reusing its value buffer, the evaluator gaining an `Evaluate` taking an outside buffer; in `Cpu`, `TranslationMode`, `TranslationOptions`, `SetTranslation`, `translation()`, `TranslationStats` with `translation_stats()`, the dispatch, the forced single step after `kInterpret`, `RegisterGate` flushing, `ActiveEngine` and `EngineMemoryBytes`, with the CMake option `REX86_FORCE_TRANSLATION`. Not in the design: `TranslationStats`, because a forced build passing every test could not show from outside that translation actually happened; the benchmark and consumers' diagnostics use it too, and design decision 4 is to record it (next steps). The robustness harness: `RunCase` takes translation settings and `RunCaseTwice` checks I8 in a third run, comparing an `architectural_digest` without the kTranslated bit and the count of code-page stores. The benchmark: `--engine evaluator` and a per-workload line of `engine_bytes` and translation counts. Unit tests in `translate_runtime_test.cpp`: the loop at thresholds 0, 4 and 32, budgets of 1, 2, 3 and 7 for 400 runs each, a fault after a failed check, a gate registered later inside a translated loop, self-modifying code and `InvalidateCode`, a stop request and turning translation off. CI: `linux-x64-translate` (GCC Debug ASan/UBSan, warm-up 0, forced translation). Wasm32 test tools link with `-sALLOW_MEMORY_GROWTH=1` (below). Failures during implementation: the forced build failed `cpu_test`'s "`ActiveEngine` is the interpreter", as expected, so the test now picks its expectation from the translation settings; 12 runtime tests failed on the test's own `jnz` displacements, off by one (0xF5 and 0xF3 for 0xF4), jumping into instructions, with translation and the interpreter still agreeing; Clang caught an unused constant under `-Werror`, which GCC does not warn about; the wasm32 forced build's unit tests stopped on OOM, while native peak RSS is 12.5 MB plain and 12.7 MB forced: the unit tests already sat near Emscripten's default 16 MiB (the benchmark test holds a 4 MiB guest and a 4.6 MB decode cache), so the test tools alone enable memory growth, as browser consumers do, translation costing about 3 KB per block on the benchmark. Mutation tests: an AF defect in the frontend gave several I8 violations within 3,000 robustness cases; removing the forced step after `kInterpret` made the robustness harness and the unit tests never end (stopped at a 120 s limit), confirming design decision 1, the harness's missing watchdog going to the analysis's unresolved items. Builds and ctest with `-Werror` in the table above: x86-64 Debug, Release and Clang Debug, x86-64 forced Debug and forced ASan/UBSan at warm-up 0 at 8/8, i386 forced Release at 9/9, wasm32 plain and forced at 6/6; 1,976 unit checks, zero failures, plain and forced. Robustness I8: 6 × 50,000 cases (seeds from 4400000) with zero violations, about 190 s per shard. Interpreter cost by cachegrind (valgrind 3.23.0 built from source in the scratchpad, against `main` at `adc0452`, host instructions per step) in the table above: the path with translation off did not grow and fell by about 19 (3.9%), estimated to come from the layout change of splitting `RunBlock` into templates; this machine's `perf_event_paranoid` is 4 and it had no valgrind. First evaluator throughput (same machine, Release, 10 frames, one run each, recorded only), interpreter to evaluator: alu 19.3 to 52.8 MIPS, memory 32.3 to 30.5, call 39.7 to 40.4, string 76.9 to 44.3, x87 17.3 to 8.4, mixed 63.2 to 47.7; the translation counts show x87 with 2 blocks and string exiting to the interpreter on every block, workloads with many uncovered instructions paying an interpreter step and a lookup per block. Estimate: with wider coverage the evaluator could be a fast path for JIT-free hosts (#42 decision 1), to be measured properly (interleaved, repeated).*

### 이어서 할 일 / Next

1. (반영함) 설계 결정 4에 `TranslationStats`를 더했다.
2. 커밋, push, CI(새 `linux-x64-translate` 포함) 확인.
3. #45(wasm 백엔드)로 넘어간다.

*Next: 1 add `TranslationStats` to design decision 4 (in the same commit); 2 commit, push and check CI, the new `linux-x64-translate` included; 3 move on to #45 (the wasm backend).*
