# #37 작업 지시 : 릴리스 tag의 fuzz 캠페인 / #37 work order : the release-tag fuzz campaign

이슈: [#37](https://github.com/reexec/rex86/issues/37) | 설계: [20261010-i037](../design/20261010-i037-tag-fuzz-campaign.md) | 로그: [20261010-i037](../work-logs/20261010-i037-tag-fuzz-campaign.md)

## 작업 항목 / Tasks

1. `scripts/fuzz_campaign.sh`(설계 결정 3, 5):
   * 도구, 규모, 시드 오프셋, shard 수를 받아 shard를 병렬로 띄우고 기다린다.
   * shard마다 로그를 남기고, 마지막 결과 줄과 CPU 모델, 걸린 시간을 `[rex86-campaign]` 줄로 모은다.
   * `$GITHUB_STEP_SUMMARY`가 있으면 요약에 쓴다. 종료 코드는 shard 중 하나라도 실패하면 실패다.
   * tag 이름에서 시드 오프셋을 계산하는 함수.
2. `.github/workflows/fuzz-campaign.yml`(결정 1, 2, 4, 5):
   * 트리거: tag `v*`, `workflow_dispatch`(규모, 시드 오프셋), 워크플로 파일을 바꾼 브랜치 push(smoke).
   * 작업 여덟: int-i386 두 개, x87과 SIMD, 견고성 Release, 견고성 ASan/UBSan, libFuzzer(먼저 ctest), 견고성 AArch64, SST.
   * 로그와 실패 입력의 artifact, libFuzzer 말뭉치 캐시.
3. 문서:
   * 가이드 `docs/guides/fuzz-campaign.md`(돌리는 법, 읽는 법, 실패 시 절차).
   * AGENTS.md 머지 절차(결정 6), ARCHITECTURE의 CI 절, 기존 fuzz 가이드에서 캠페인으로의 링크.
   * 작업 로그.
4. 검증(설계의 검증 절):
   * 스크립트의 로컬 smoke와 실패 경우.
   * 브랜치 push의 smoke 캠페인이 모든 작업에서 녹색.

*1 `scripts/fuzz_campaign.sh` (design decisions 3 and 5): takes the tool, scale, seed offset and shard count, starts the shards in parallel and waits; keeps a log per shard and gathers the last result lines, the CPU model and elapsed time as `[rex86-campaign]` lines; writes them to the summary when `$GITHUB_STEP_SUMMARY` is set; fails if any shard fails; a function computing the seed offset from a tag name. 2 `.github/workflows/fuzz-campaign.yml` (decisions 1, 2, 4 and 5): triggers on `v*` tags, `workflow_dispatch` (scale, seed offset) and branch pushes changing the workflow files (smoke); eight jobs: two int-i386, x87 and SIMD, robustness Release, robustness ASan/UBSan, libFuzzer (ctest first), robustness AArch64, SST; artifacts of logs and failing inputs, and a libFuzzer corpus cache. 3 documents: the guide `docs/guides/fuzz-campaign.md` (running, reading, the procedure on failure); AGENTS.md's merge procedure (decision 6), ARCHITECTURE's CI section, links from the existing fuzz guides to the campaign; the work log. 4 verification (the design's verification section): the script's local smoke and failure cases; the branch push's smoke campaign green in every job.*

## 완료 조건 / Completion criteria

* 브랜치 push의 smoke 캠페인이 여덟 작업 모두 녹색이고, 요약에 시드, 건수, 결과, CPU 모델이 나온다.
* 스크립트가 shard 하나의 실패를 작업 실패로 올린다(로컬에서 확인).
* 문서가 tag push 뒤의 확인 절차와 실패 시 절차를 설명한다.
* 브랜치 CI(`ci.yml`)는 바뀌지 않고 녹색이다.
* 전체 규모의 실제 시간은 머지 뒤 첫 실행에서 재고 로그에 더한다(결정 2).

*The branch push's smoke campaign is green in all eight jobs, its summary showing seeds, case counts, results and the CPU model; the script turns one failing shard into a failing job (checked locally); the documents explain the check after a tag push and the procedure on failure; the branch CI (`ci.yml`) is unchanged and green; the real times at full scale are measured at the first run after merging and added to the log (decision 2).*
