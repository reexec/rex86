# #59 작업 지시 : 작은 후속 정리 / #59 work order : small follow-ups

이슈: [#59](https://github.com/reexec/rex86/issues/59) | 설계: [20261013-i059](../design/20261013-i059-small-follow-ups.md) | 로그: [20261013-i059](../work-logs/20261013-i059-small-follow-ups.md)

## 작업 항목 / Tasks

1. `rex86_robust`에 케이스별 시간 감시를 넣는다. `--case-seconds`, 시험용 `--test-stall-ms`, ctest `rex86_robust_watchdog`, `Threads::Threads` 연결(설계 결정 1).
2. `compare.html`과 `compare.js`에 A/A 대조(A2)를 넣는다. `drive_pages.mjs`에 `--control`과 대조 확인을 더한다(결정 2).
3. 세 워크플로의 러너 라벨을 `ubuntu-26.04`와 `ubuntu-26.04-arm`으로 명시하고, 새 이미지에서 CI와 캠페인 smoke를 확인한다. 깨지는 것이 있으면 원인을 찾아 고친다(결정 3).
4. README의 `GIT_TAG`, #53 작업 로그의 캠페인 결과(결정 4).
5. 문서: 분석 `robustness-fuzz.md`, 가이드 `robustness-fuzz.md`와 `browser-tests.md`, `ARCHITECTURE.md`, 작업 로그.

*1 a per-case watchdog in `rex86_robust`: `--case-seconds`, `--test-stall-ms` for tests, ctest `rex86_robust_watchdog`, linking `Threads::Threads` (design decision 1); 2 an A/A control (A2) in `compare.html` and `compare.js`, with `--control` and a control check in `drive_pages.mjs` (decision 2); 3 name the runner labels `ubuntu-26.04` and `ubuntu-26.04-arm` in all three workflows and check CI and the campaign smoke on the new image, finding and fixing the cause of anything that breaks (decision 3); 4 the README's `GIT_TAG` and #53's campaign results (decision 4); 5 documents: the analysis and guide `robustness-fuzz.md`, the guide `browser-tests.md`, `ARCHITECTURE.md` and the work log.*

## 완료 조건 / Completion criteria

* 로컬 x86-64 ctest와 TSan 빌드의 감시 시험, 조립한 Pages 사이트의 `drive_pages.mjs`(대조 켬, 끔, 버전 불일치)가 통과한다.
* 이 브랜치의 CI와 캠페인 smoke가 Ubuntu 26.04에서 녹색이다.

*Local x86-64 ctest, the watchdog test in a TSan build and `drive_pages.mjs` on an assembled Pages site (control on, off and a mismatched version) pass, and this branch's CI and campaign smoke are green on Ubuntu 26.04.*
