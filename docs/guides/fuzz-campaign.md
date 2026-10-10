# 가이드 : 릴리스 fuzz 캠페인 / Guide : the release fuzz campaign

근거: [#37 설계](../design/20261010-i037-tag-fuzz-campaign.md) | 로그: [20261010-i037](../work-logs/20261010-i037-tag-fuzz-campaign.md) | 개별 도구: [정수와 trace](integer-host-fuzz-and-traces.md), [x87](x87-host-fuzz.md), [SIMD](simd-host-fuzz.md), [견고성](robustness-fuzz.md), [SST](singlesteptests.md)

워크플로 `fuzz-campaign`(`.github/workflows/fuzz-campaign.yml`)은 호스트 대조 fuzz, 견고성 하네스, libFuzzer, SingleStepTests를 릴리스 규모로 GitHub Actions에서 돌립니다. 브랜치 CI(`ci`)의 smoke보다 수백 배 큰 규모를 릴리스마다 같게 돌려 기록으로 남기는 것이 목적입니다.

*The `fuzz-campaign` workflow (`.github/workflows/fuzz-campaign.yml`) runs the host-comparison fuzzes, the robustness harness, libFuzzer and SingleStepTests at release scale on GitHub Actions, to run and record the same scale, hundreds of times the branch CI's (`ci`) smokes, for every release.*

## 언제 도나 / When it runs

| 계기 | 규모 | 시드 오프셋 |
|---|---|---|
| tag `v*` push | 전체 | 버전 × 10^8 (v0.0.19는 1,900,000,000) |
| Actions 탭의 "Run workflow"(`workflow_dispatch`) | 고름(`full`, `smoke`) | 입력값(기본 0) |
| 캠페인 파일(워크플로, `scripts/fuzz_campaign.sh`)을 바꾼 브랜치 push | smoke | 0 |

머지 전에 큰 변경을 전체 규모로 보고 싶으면 Actions 탭에서 `fuzz-campaign`을 고르고 브랜치를 지정해 "Run workflow"를 누릅니다.

*A `v*` tag push runs it at full scale with the version × 10^8 as the seed offset (1,900,000,000 for v0.0.19); "Run workflow" on the Actions tab (`workflow_dispatch`) runs it at the chosen scale (`full`, `smoke`) with the given offset (default 0); a branch push changing the campaign's files (the workflow, `scripts/fuzz_campaign.sh`) runs it at smoke scale with offset 0. To see a large change at full scale before merging, pick `fuzz-campaign` on the Actions tab, choose the branch and press "Run workflow".*

## 작업과 규모 / Jobs and scale

| 작업 | 내용 | 전체 규모 |
|---|---|---|
| `plan` | 규모와 시드 오프셋 결정 | |
| `int-i386` (예열 기본값, 0) | 정수 호스트 대조, `-m32` Release | 각 2,000만 |
| `x87-simd` | x87 x86-64, SIMD x86-64와 i386 | 1억, 1억, 2,000만 |
| `robust-release` | 견고성 하네스 x86-64 Release | 100만 |
| `robust-asan` | 견고성 하네스 GCC ASan/UBSan, 예열 0 | 10만 |
| `libfuzzer` | 그 구성의 ctest 전체, 다음으로 libFuzzer `-fork=4` | 30분 |
| `robust-arm64` | 견고성 하네스 AArch64 Release | 50만 |
| `sst` | SingleStepTests/80386 real mode `--execute` | 941 파일 |

규모는 첫 실행의 실제 시간으로 고칩니다([설계](../design/20261010-i037-tag-fuzz-campaign.md) 결정 2). 바뀐 값은 워크플로가 기준입니다.

*Jobs: `plan` decides the scale and seed offset; `int-i386` (default warm-up and 0), the integer host comparison in a `-m32` Release build, 20M each; `x87-simd`, x87 on x86-64 and SIMD on x86-64 and i386, 100M, 100M and 20M; `robust-release`, the robustness harness in x86-64 Release, 1M; `robust-asan`, under GCC ASan/UBSan at warm-up 0, 100,000; `libfuzzer`, the whole ctest in its configuration, then libFuzzer with `-fork=4`, 30 minutes; `robust-arm64`, the robustness harness in AArch64 Release, 500,000; `sst`, SingleStepTests/80386 real mode `--execute`, 941 files. The scales are corrected from the first run's real times (design decision 2); the workflow holds the current values.*

## 결과 읽기 / Reading the results

1. 실행 페이지의 **Summary**에 작업마다 `### <작업> (ok)` 또는 `(FAILED)` 절이 있습니다. 첫 줄은 러너의 CPU 모델이고, 그 아래 shard마다 한 줄씩 `[rex86-campaign]` 줄이 있습니다.

   ```
   [rex86-campaign] job=int-i386-warmup-default shard=0 seed=1930000000 cases=5000000 exit=0 seconds=600 | forms=1112 iterations=5000000 seed=1930000000 mismatches=0 vendor_deviations=0 ...
   ```

2. 판정은 `exit=`입니다. 0이 아니면 그 작업이 빨갛습니다. 각 도구의 판정 그대로이며, 호스트 대조 fuzz는 `mismatches`, 견고성 하네스는 `violations`, SST는 `mismatches`, libFuzzer는 크래시와 시간 초과입니다.
3. **제조사 차이는 실패가 아닙니다.** 정수의 `vendor_deviations`, x87의 `vendor_deviations`(SDM 이탈)와 `within_tolerance`, SIMD의 `within_tolerance`는 CPU 모델과 함께 보고만 합니다. 정수의 `vendor_deviations`는 AMD에서만 나오는 폴트 순서를 셉니다([분석](../analysis/integer-host-comparison.md)). Intel 러너에서 0이 아니면 새 사실이므로 분석 문서에 적고 원인을 봅니다.
4. 로그 전부는 artifact `campaign-<작업>`에 있습니다(shard마다 `<작업>-<k>.log`, 정수의 `int-<k>.rxt`, libFuzzer의 `crashes/`).

*The run page's **Summary** has a `### <job> (ok)` or `(FAILED)` section per job: the runner's CPU model first, then one `[rex86-campaign]` line per shard (example above). The verdict is `exit=`: nonzero turns the job red, by each tool's own verdict (`mismatches` for the host comparisons and SST, `violations` for the robustness harness, a crash or timeout for libFuzzer). Vendor differences are not failures: the integer `vendor_deviations`, the x87's `vendor_deviations` (SDM deviations) and `within_tolerance`, and SIMD's `within_tolerance` are reported next to the CPU model; the integer `vendor_deviations` count fault orders only AMD shows ([analysis](../analysis/integer-host-comparison.md)), so a nonzero count on an Intel runner is a new fact for the analysis to record and explain. Every log is in the `campaign-<job>` artifact (`<job>-<k>.log` per shard, the integer fuzz's `int-<k>.rxt`, libFuzzer's `crashes/`).*

## 실패했을 때 / On failure

1. 요약의 실패 줄에서 시드를 읽고 로컬에서 재현합니다.
   * 정수: artifact의 `int-<k>.rxt`를 `rex86_trace --dump <case> <파일>`로 봅니다. 또는 `rex86_int_fuzz <건수> <시드>`를 다시 돌립니다.
   * x87, SIMD: `rex86_x87_fuzz <건수> <시드> --verbose`처럼 같은 건수와 시드로 다시 돌립니다.
   * 견고성: 로그의 `VIOLATION seed=<S>`를 `rex86_robust --case <S>`로 돌립니다.
   * libFuzzer: `crashes/`의 파일을 `rex86_robust_libfuzzer <파일>`에 줍니다.
   * SST: 로그의 실패 파일을 `rex86_sst <파일> --execute --verbose`로 돌립니다.
2. **tag는 지우거나 옮기지 않습니다.** 소비자의 FetchContent가 이미 가리켰을 수 있습니다.
3. 이슈를 만들고 실행 링크, 재현 시드, CPU 모델을 적습니다. 고친 뒤 patch 릴리스를 냅니다.
4. 소비자의 tag 올림 작업은 캠페인이 녹색인 tag로만 합니다.

*Read the seed off the failing line and reproduce locally: for the integer fuzz, `rex86_trace --dump <case> <file>` on the artifact's `int-<k>.rxt`, or rerun `rex86_int_fuzz <cases> <seed>`; for x87 and SIMD, rerun with the same count and seed, e.g. `rex86_x87_fuzz <cases> <seed> --verbose`; for the robustness harness, `rex86_robust --case <S>` with the log's `VIOLATION seed=<S>`; for libFuzzer, pass a `crashes/` file to `rex86_robust_libfuzzer <file>`; for SST, `rex86_sst <file> --execute --verbose` on the failing file. Never delete or move the tag, which consumers' FetchContent may already point at. Open an issue with the run link, the reproducing seeds and the CPU model, and release a patch after the fix. Consumers' tag bumps only take tags whose campaign is green.*

## 로컬에서 / Locally

`scripts/fuzz_campaign.sh`는 로컬에서도 같게 돕니다. `{CASES}`, `{SEED}`, `{SHARD}`가 shard마다 바뀝니다.

```bash
scripts/fuzz_campaign.sh offset v0.0.19            # 1900000000
CAMPAIGN_LOG_DIR=/tmp/campaign scripts/fuzz_campaign.sh run int 4 1000000 30000000 1 -- \
    ./build/linux-x86-release/bin/rex86_int_fuzz {CASES} {SEED} --failures /tmp/campaign/int-{SHARD}.rxt
```

*`scripts/fuzz_campaign.sh` runs the same way locally, `{CASES}`, `{SEED}` and `{SHARD}` replaced per shard (example above).*

## 릴리스 노트에 적기 / Recording in the release notes

tag push 뒤에 캠페인 결과를 확인하고, 다음 릴리스 노트의 검증 절에 앞 릴리스의 캠페인(실행 링크, 결과, CPU 모델, 규모)을 적습니다. squash 커밋 ID를 다음 노트에서 보완하는 것과 같은 방식입니다(AGENTS.md 브랜치와 머지 규칙).

*After the tag push, check the campaign, and record the previous release's campaign (run link, result, CPU model, scale) in the next release notes' validation section, the way the squash commit ID is filled in by the next notes (AGENTS.md, branch and merge rules).*
