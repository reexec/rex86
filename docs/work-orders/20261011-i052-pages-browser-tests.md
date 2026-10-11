# #52 작업 지시 : GitHub Pages의 버전별 브라우저 테스트와 벤치마크 비교 / #52 work order : versioned browser tests and benchmark comparison on GitHub Pages

이슈: [#52](https://github.com/reexec/rex86/issues/52) | 설계: [20261011-i052](../design/20261011-i052-pages-browser-tests.md) | 로그: [20261011-i052](../work-logs/20261011-i052-pages-browser-tests.md)

## 작업 항목 / Tasks

1. `tests/host/web/pages/`: 버전 목록 `index.html`, 비교 `compare.html`와 `compare.js`(설계 결정 3, 4). 보고 함수는 `report.mjs`를 함께 복사해 쓴다.
2. `scripts/pages_site.sh`: 사이트 디렉터리에 버전 디렉터리를 조립하고, `versions.json`을 합치고, 루트를 조건에 따라 갱신하고, 500 MB 예산을 넘으면 오래된 버전을 지운다(결정 1, 2).
3. `serve.mjs --site <디렉터리>`: 조립한 사이트를 그대로 내준다(결정 5).
4. `.github/workflows/pages.yml`: `v*` tag push와 `workflow_dispatch(version)`, 빌드, 조립, 고아 커밋 force push, 작업 요약에 크기 기록(결정 2).
5. CI `browsers` 작업에 비교 smoke를 더한다(같은 빌드를 두 버전 이름으로, Chromium, `rounds=1&frames=2`). `drive.mjs`나 작은 드라이버로 `[rex86-compare] result=ok`를 확인한다.
6. 계약 주석: `tests/host/web/worker.js`, `src/tools/bench/main.cpp`(결정 4의 계약).
7. 로컬 검증: 이 기계에 emsdk 3.1.74, Node, Ninja를 갖추고 브라우저용 빌드를 만든다. 사이트를 조립해 Chromium과 Firefox에서 목록, 벤치마크 링크, 비교를 확인한다. v0.0.23 버전 디렉터리의 크기를 잰다.
8. 문서: 가이드 `browser-tests.md` 3절(Pages 주소를 기본 경로로, LAN 서버는 미릴리스 빌드용), 새 4절(버전 비교), README 링크, ARCHITECTURE, 기록 `browser-wasm.md` 5절의 안내, 작업 로그.
9. 머지와 tag push 뒤(사용자): 저장소 설정에서 Pages 원본을 `gh-pages` `/`로 정한다. v0.0.24 tag를 ref로 `version=0.0.23` dispatch를 돌려 채워 넣는다. Pages 주소를 확인한다.

*1 `tests/host/web/pages/`: the version list `index.html` and the comparison `compare.html` with `compare.js` (design decisions 3 and 4), copying `report.mjs` alongside for the report functions; 2 `scripts/pages_site.sh`: assembles a version directory into a site directory, merges `versions.json`, refreshes the root when allowed and removes the oldest versions past the 500 MB budget (decisions 1 and 2); 3 `serve.mjs --site <dir>` serving an assembled site as is (decision 5); 4 `.github/workflows/pages.yml`: `v*` tag pushes and `workflow_dispatch(version)`, build, assembly, an orphan-commit force push and the size in the job summary (decision 2); 5 a comparison smoke in CI's `browsers` job (one build under two version names, Chromium, `rounds=1&frames=2`), `drive.mjs` or a small driver checking `[rex86-compare] result=ok`; 6 contract comments in `tests/host/web/worker.js` and `src/tools/bench/main.cpp` (decision 4's contract); 7 local verification: emsdk 3.1.74, Node and Ninja on this machine, a browser build, an assembled site checked in Chromium and Firefox for the list, a benchmark link and a comparison, and v0.0.23's version directory measured for size; 8 documents: the guide `browser-tests.md` section 3 (the Pages address as the default path, the LAN server for unreleased builds) and a new section 4 (version comparison), README links, ARCHITECTURE, a note in section 5 of the record `browser-wasm.md`, the work log; 9 after the merge and tag push (by the user): set the Pages source to `gh-pages` `/` in the repository settings, backfill with a `version=0.0.23` dispatch on the v0.0.24 tag, and check the Pages address.*

## 완료 조건 / Completion criteria

* 로컬에서 조립한 사이트가 Chromium과 Firefox에서 목록, 벤치마크, 비교를 통과한다. 비교는 `[rex86-compare] result=ok`를 내고, 버전 이름이 출력과 다르면 실패로 센다.
* CI `browsers` 작업의 비교 smoke와 다섯 호스트 CI가 녹색이다.
* `pages.yml`이 문법 검사(`actionlint`가 있으면)를 통과한다. 실제 배포는 tag push 뒤에 확인하고 작업 로그에 날짜별 절로 더한다.
* 실기기 결과는 사용자가 돌린 뒤 기록 5절에 더한다.

*An assembled site passes the list, a benchmark and a comparison locally in Chromium and Firefox, the comparison printing `[rex86-compare] result=ok` and counting a version name that differs from the output as a failure; CI's `browsers` comparison smoke and the five-host CI are green; `pages.yml` passes a syntax check (`actionlint` if available), the actual deployment being checked after the tag push and added to the work log as a dated section; real-device results join record section 5 once the user has run them.*
