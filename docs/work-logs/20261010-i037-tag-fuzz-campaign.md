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
- **CI의 smoke 캠페인**([fuzz-campaign run 1](https://github.com/reexec/rex86/actions/runs/38011083921), 커밋 `9422521`):
  - 러너 CPU: AMD EPYC 7763(Zen 3).
  - SST 8파일 4 shard 불일치 0, 나머지 작업은 아래 표에 정리한다.

*Context: `ci.yml`'s push filter is `branches: ['**']`, so it never runs on a tag push and a new workflow can own tags without overlap; the three host-comparison fuzzes, the robustness harness and the SST runner already report failure with exit code 1, and the campaign uses those verdicts as they are; the repository is public, so standard runners (4 vCPUs) are available, with throughputs estimated from #32's Intel Xeon VM. Design approved by the user: decisions 1 to 6 as written, on the task branch `work/i037-tag-fuzz-campaign` per AGENTS.md (its push was allowed). Implemented: `scripts/fuzz_campaign.sh`, whose `offset <tag>` computes version × 10^8 and whose `run` starts a command per shard (`{CASES}`, `{SEED}`, `{SHARD}` replaced), writes a `[rex86-campaign]` line per shard with the CPU model and the job summary, and ends with 1 if any shard fails; `.github/workflows/fuzz-campaign.yml` with `plan` and eight jobs (two int-i386, x87-simd, robust-release, robust-asan, libfuzzer, robust-arm64, sst), triggered by `v*` tags, `workflow_dispatch` and branch pushes changing the campaign's files (smoke). Design decision 3 was aligned with the implementation: the offset is version × 10^8, the per-job base seeds lie 10^7 apart, and libFuzzer's 32-bit `-seed` takes the offset modulo 2^32. SST deals its files into four shard directories under the suite root, since the runner reads `80386.csv` from a directory's parent. Documents: the guide `fuzz-campaign.md`, AGENTS.md's branch and merge rules (the check after a tag push, the procedure on failure, the record in the next release notes), ARCHITECTURE section 4, and links to the campaign from the five fuzz guides. Local verification on the Intel Xeon VM: `offset v0.0.19` = 1,900,000,000, `offset v1.2.3` = 1,020,300,000,000, `offset main` exits 2; the integer fuzz at 4 shards × 20,000 and the robustness harness at 2 shards × 300 give the right lines and summary file with exit 0; deliberate failures, one shard exiting 1 and a missing binary (127), both show the last 20 lines and exit 1, and too few arguments print the usage and exit 2; libFuzzer with `-fork=4` for 20 seconds exits 0 with `INFO: fuzzed for` and `INFO: exiting` as its result lines, which the result pattern now includes; SST with 8 files in four shards ran 3,500 to 5,000 tests each with zero mismatches and found `80386.csv` (no f_umask note). The CI smoke campaign (run 1, commit `9422521`) ran on an AMD EPYC 7763 (Zen 3); SST's 8 files in 4 shards had zero mismatches, and the other jobs are in the table below.*
