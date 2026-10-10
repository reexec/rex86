# #37 작업 로그 : 릴리스 tag의 fuzz 캠페인 / #37 work log : the release-tag fuzz campaign

이슈: [#37](https://github.com/reexec/rex86/issues/37) | 설계: [20261010-i037](../design/20261010-i037-tag-fuzz-campaign.md) | 지시서: [20261010-i037](../work-orders/20261010-i037-tag-fuzz-campaign.md) | 가이드: [fuzz 캠페인](../guides/fuzz-campaign.md)

## 2026-10-10

- **맥락 확인**:
  - `ci.yml`의 push 필터는 `branches: ['**']`라 tag push에는 돌지 않는다. 그래서 새 워크플로가 tag를 맡아도 겹치지 않는다.
  - 호스트 대조 fuzz 셋과 견고성 하네스, SST 러너는 이미 실패를 종료 코드 1로 알린다. 캠페인은 이 판정을 그대로 쓴다.
  - 저장소는 public이라 표준 러너(4 vCPU)를 쓸 수 있다. 처리량은 #32의 Intel Xeon VM에서 잰 값으로 추정했다.
- **설계 승인**(사용자): 결정 1~6을 그대로 구현한다. 작업 브랜치는 AGENTS.md 규칙대로 `work/i037-tag-fuzz-campaign`이다(push가 허용됐다).
- **구현**:
  - `scripts/fuzz_campaign.sh`: `offset <tag>`는 버전 × 10^8을 계산한다. `run`은 shard마다 명령을 띄우고(`{CASES}`, `{SEED}`, `{SHARD}` 치환), shard마다 `[rex86-campaign]` 줄과 CPU 모델, 작업 요약을 쓰며, shard 하나라도 실패하면 1로 끝난다.
  - `.github/workflows/fuzz-campaign.yml`: `plan`과 작업 여덟(int-i386 둘, x87-simd, robust-release, robust-asan, libfuzzer, robust-arm64, sst). 트리거는 tag `v*`, `workflow_dispatch`, 캠페인 파일을 바꾼 브랜치 push(smoke)다.
  - 설계 결정 3을 구현에 맞게 고쳤다. 오프셋은 버전 × 10^8이고, 작업별 기본 시드는 10^7 간격이다. libFuzzer의 `-seed`는 32비트라 오프셋을 2^32로 나눈 나머지를 쓴다.
  - SST는 파일을 네 shard 디렉터리에 번갈아 풀어 둔다. 러너가 디렉터리의 부모에서 `80386.csv`를 읽으므로 shard 디렉터리는 스위트 루트 아래에 둔다.
  - 문서: 가이드 `fuzz-campaign.md`, AGENTS.md 브랜치와 머지 규칙(tag push 뒤 확인, 실패 시 절차, 다음 릴리스 노트에 기록), ARCHITECTURE 4절, 개별 fuzz 가이드 다섯에서 캠페인으로의 링크.
- **로컬 검증**(Intel Xeon VM):
  - `offset v0.0.19` = 1,900,000,000, `offset v1.2.3` = 1,020,300,000,000, `offset main`은 종료 코드 2.
  - 정수 fuzz 4 shard × 2만 건, 견고성 2 shard × 300건: 요약 줄과 요약 파일이 맞게 나오고 종료 코드 0.
  - 일부러 실패: shard 하나가 1로 끝나는 경우와 없는 바이너리(127) 모두 마지막 20줄을 보이고 종료 코드 1. 인자가 모자라면 사용법과 2.
  - libFuzzer `-fork=4` 20초: 종료 코드 0, 결과 줄은 `INFO: fuzzed for`와 `INFO: exiting`. 이 두 줄을 결과 줄 패턴에 더했다.
  - SST 8파일을 네 shard로: 각 3,500~5,000건 실행, 불일치 0, `80386.csv`를 찾았다(f_umask 경고 없음).
- **CI의 smoke 캠페인**([fuzz-campaign run 1](https://github.com/reexec/rex86/actions/runs/38011083921), 커밋 `9422521`): `plan`과 여덟 작업 모두 녹색이다.

  | 작업 | 규모(smoke) | 결과 | 러너 CPU(확인된 것) |
  |---|---|---|---|
  | int-i386 예열 기본값 | 4 × 2만 | 불일치 0, `vendor_deviations` 0 | |
  | int-i386 예열 0 | 4 × 2만 | 불일치 0, shard 3에서 `vendor_deviations=2` | |
  | x87-simd | 각 4 × 2만 | 불일치 0, SIMD 허용 420~464 | AMD EPYC 9V45(Zen 5) |
  | robust-release | 4 × 500 | 위반 0 | |
  | robust-asan | 4 × 100 | 위반 0 | |
  | libfuzzer | ctest 7/7, 60초 | 크래시 0 | AMD EPYC 9V74 |
  | robust-arm64 | 4 × 500 | 위반 0 | |
  | sst | 8 파일 4 shard | 불일치 0 | AMD EPYC 7763(Zen 3) |

  - 작업마다 러너 CPU가 달랐다(Zen 3, Zen 5 등). 설계 결정 4에서 CPU 모델을 함께 적고 문턱을 두지 않기로 한 근거가 확인됐다.
  - 예열 0 작업의 `vendor_deviations=2`는 AMD 러너에서 예상되는 값이다([분석](../analysis/integer-host-comparison.md)의 Zen 3 BOUND, Zen 5 CMPS). 실패로 치지 않았다.
  - libFuzzer 구성의 ctest가 CI에서 처음 돌았고 7/7 통과했다(#32의 `HostPointer` 수정 뒤).
  - smoke와 `workflow_dispatch`의 기본 오프셋 0에서 libFuzzer는 `-seed=0`을 받는데, libFuzzer에서 0은 "무작위 시드"다. fork 모드의 libFuzzer는 원래 재현 가능한 실행이 아니므로(작업자 스케줄에 따라 다름) 그대로 두고, 재현은 크래시 입력 파일로 한다.
  - 전체 규모의 실제 시간은 머지 뒤 첫 tag(또는 `workflow_dispatch`)에서 재서 이 로그에 날짜별 절로 더한다.
- **후속 후보**(이 작업의 범위 밖):
  - 브랜치 CI의 `linux-x64-libfuzzer` 작업에 그 구성의 ctest를 더할지(설계 결정 2).
  - GitHub이 Node.js 20 기반 action(`actions/checkout@v4`, `upload-artifact@v4`, `cache@v4`)을 Node.js 24로 강제 실행한다는 경고를 낸다. `ci.yml`도 같다. 새 주 버전으로 올리는 일은 두 워크플로를 함께 다루는 별도 작업이 맞다.
  - `GuestMemory`의 null base 접근자(#32 로그의 미확정 항목).

*Context: `ci.yml`'s push filter is `branches: ['**']`, so it never runs on a tag push and a new workflow can own tags without overlap; the three host-comparison fuzzes, the robustness harness and the SST runner already report failure with exit code 1, and the campaign uses those verdicts as they are; the repository is public, so standard runners (4 vCPUs) are available, with throughputs estimated from #32's Intel Xeon VM. Design approved by the user: decisions 1 to 6 as written, on the task branch `work/i037-tag-fuzz-campaign` per AGENTS.md (its push was allowed). Implemented: `scripts/fuzz_campaign.sh`, whose `offset <tag>` computes version × 10^8 and whose `run` starts a command per shard (`{CASES}`, `{SEED}`, `{SHARD}` replaced), writes a `[rex86-campaign]` line per shard with the CPU model and the job summary, and ends with 1 if any shard fails; `.github/workflows/fuzz-campaign.yml` with `plan` and eight jobs (two int-i386, x87-simd, robust-release, robust-asan, libfuzzer, robust-arm64, sst), triggered by `v*` tags, `workflow_dispatch` and branch pushes changing the campaign's files (smoke). Design decision 3 was aligned with the implementation: the offset is version × 10^8, the per-job base seeds lie 10^7 apart, and libFuzzer's 32-bit `-seed` takes the offset modulo 2^32. SST deals its files into four shard directories under the suite root, since the runner reads `80386.csv` from a directory's parent. Documents: the guide `fuzz-campaign.md`, AGENTS.md's branch and merge rules (the check after a tag push, the procedure on failure, the record in the next release notes), ARCHITECTURE section 4, and links to the campaign from the five fuzz guides. Local verification on the Intel Xeon VM: `offset v0.0.19` = 1,900,000,000, `offset v1.2.3` = 1,020,300,000,000, `offset main` exits 2; the integer fuzz at 4 shards × 20,000 and the robustness harness at 2 shards × 300 give the right lines and summary file with exit 0; deliberate failures, one shard exiting 1 and a missing binary (127), both show the last 20 lines and exit 1, and too few arguments print the usage and exit 2; libFuzzer with `-fork=4` for 20 seconds exits 0 with `INFO: fuzzed for` and `INFO: exiting` as its result lines, which the result pattern now includes; SST with 8 files in four shards ran 3,500 to 5,000 tests each with zero mismatches and found `80386.csv` (no f_umask note). The CI smoke campaign (run 1, commit `9422521`): `plan` and all eight jobs green, as in the table above. The runner CPU differed between jobs (Zen 3, Zen 5 and others), which confirms decision 4's choice to record the CPU model and set no threshold. The warm-up 0 job's `vendor_deviations=2` is the value expected on an AMD runner (Zen 3's BOUND and Zen 5's CMPS in the [analysis](../analysis/integer-host-comparison.md)) and did not count as a failure. The ctest in the libFuzzer configuration ran in CI for the first time and passed 7/7 (after #32's `HostPointer` fix). At offset 0, the default of smoke and `workflow_dispatch`, libFuzzer receives `-seed=0`, which libFuzzer takes as a random seed; libFuzzer in fork mode is not a reproducible run anyway (it depends on worker scheduling), so it stays, and crashes are reproduced from their input files. The real times at full scale are measured at the first tag (or `workflow_dispatch`) after merging and added here as a dated section. Follow-up candidates outside this task: adding the ctest of that configuration to the branch CI's `linux-x64-libfuzzer` job (design decision 2); GitHub warns that the Node.js 20 actions (`actions/checkout@v4`, `upload-artifact@v4`, `cache@v4`) are forced onto Node.js 24, in `ci.yml` too, and moving to new major versions is a separate task covering both workflows; `GuestMemory`'s null-base accessors (#32's unresolved item).*

## 2026-10-10 (후속 / follow-up)

사용자 요청으로 앞 절의 후속 후보 가운데 둘을 이 작업에서 처리했다.

- **브랜치 CI의 libFuzzer 작업에 ctest**: `linux-x64-libfuzzer`가 퍼저 타깃만 빌드하던 것을 전체 빌드로 바꾸고, 퍼저 전에 `ctest --test-dir build/libfuzzer`를 돌린다. Clang `-O2` + UBSan 구성의 단위 테스트가 이제 모든 브랜치 push에서 돈다. 설계 결정 2를 고쳤다.
- **Node.js 20 사용 중단 경고**(설계 결정 7): 두 워크플로의 action을 `node24` 주 버전으로 올렸다. checkout v4 → v7, upload-artifact v4 → v7, cache/restore와 cache/save v4 → v6, setup-emsdk v14 → v16. 각 버전의 `action.yml`에서 `runs.using: node24`를 확인했고, 릴리스 노트의 깨지는 변경이 이 저장소의 쓰임에 해당하지 않음을 확인했다(설계 결정 7).
- 남은 후속: `GuestMemory`의 null base 접근자.

*At the user's request two of the previous section's follow-up candidates were handled in this task. The branch CI's libFuzzer job now builds everything instead of the fuzzer target alone and runs `ctest --test-dir build/libfuzzer` before fuzzing, so the unit tests under Clang `-O2` with UBSan run on every branch push (design decision 2 corrected). The Node.js 20 deprecation (design decision 7): both workflows' actions moved to `node24` major versions, checkout v4 to v7, upload-artifact v4 to v7, cache/restore and cache/save v4 to v6, setup-emsdk v14 to v16, each version's `action.yml` checked for `runs.using: node24` and the release notes' breaking changes checked against this repository's use (design decision 7). Remaining follow-up: `GuestMemory`'s null-base accessors.*
