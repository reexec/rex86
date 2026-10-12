# #53 작업 지시 : v0.0.23 캠페인의 CMPS 판정과 하네스 세그폴트 / #53 work order : v0.0.23's campaign — the CMPS judgment and the harness segfault

이슈: [#53](https://github.com/reexec/rex86/issues/53) | 설계: [20261012-i053](../design/20261012-i053-campaign-cmps-and-segfault.md) | 로그: [20261012-i053](../work-logs/20261012-i053-campaign-cmps-and-segfault.md)

## 작업 항목 / Tasks

1. v0.0.23 캠페인의 아티팩트를 받아 실패 사례 trace와 샤드 로그를 본다.
2. `SourceSegmentFirstCompareFault`의 목적지 조건을 "일부라도 영역 밖"으로 넓힌다(설계 결정 1).
3. 세그폴트 두 시드를 캠페인과 같은 빌드로 재현해 본다. 현재 `main`과 v0.0.23 소스로 반복하고 ASLR을 끈 실행도 한다. 코어만 재생하는 ASan/UBSan 빌드도 돌린다(설계 결정 2).
4. 관측 수단을 넣는다.
   - `rex86_int_fuzz`: 줄 단위 stdout, `--progress`, `--from`, 빵부스러기, 처리기 재진입 감지.
   - `fuzz_campaign.sh`: 신호로 죽은 샤드 표시, core와 gdb가 있을 때 백트레이스.
   - 워크플로: core_pattern, `--progress 250000`, 바이너리 보존.
5. 문서: 분석 `integer-host-comparison.md`, 가이드 `integer-host-fuzz-and-traces.md`, 작업 로그.

*1 fetch the v0.0.23 campaign's artifacts and read the failing traces and shard logs; 2 widen `SourceSegmentFirstCompareFault`'s destination condition to "any part outside the region" (design decision 1); 3 try to reproduce the two segfaulting seeds with the campaign's builds, repeating on today's `main` and v0.0.23's source, without ASLR as well, and run core-only replays in ASan/UBSan builds (decision 2); 4 add observability: line-buffered stdout, `--progress`, `--from`, breadcrumbs and handler re-entry detection in `rex86_int_fuzz`; signal deaths named and a gdb backtrace with a core in `fuzz_campaign.sh`; core_pattern, `--progress 250000` and the binary kept in the workflow; 5 documents: the analysis `integer-host-comparison.md`, the guide `integer-host-fuzz-and-traces.md` and the work log.*

## 완료 조건 / Completion criteria

* 정수 fuzz의 기본 실행과 ctest(i386)가 통과하고, 캠페인 스크립트와 워크플로의 새 경로를 시험한다. CI가 녹색이다.
* 머지 뒤 patch 릴리스 tag의 캠페인에서 int-i386이 녹색이면 이슈를 닫는다. 실패하면 새 관측 수단의 기록으로 이 이슈에서 이어 간다.

*The integer fuzz's default run and ctest (i386) pass, the campaign script's and workflow's new paths are exercised, and CI is green; after the merge, the issue closes when the patch release tag's campaign has int-i386 green, a failure continuing here with what the new observability records.*
