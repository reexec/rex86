# #52 작업 로그 : GitHub Pages의 버전별 브라우저 테스트와 벤치마크 비교 / #52 work log : versioned browser tests and benchmark comparison on GitHub Pages

이슈: [#52](https://github.com/reexec/rex86/issues/52) | 설계: [20261011-i052](../design/20261011-i052-pages-browser-tests.md) | 지시서: [20261011-i052](../work-orders/20261011-i052-pages-browser-tests.md) | 가이드: [브라우저 테스트](../guides/browser-tests.md) 3, 4절

## 2026-10-11

호스트: AMD Ryzen 5 5600X(Zen 3) 12 스레드, Windows 11 Pro, emsdk 3.1.74(그 안의 Node 24.19.0), Ninja 1.13.2(pip), Playwright 1.63.0(헤드리스 Chromium 153.0.8010.12, Firefox 155.0), actionlint 1.7.12. 브랜치는 `work/i052-pages-browser-tests`(main `8303ee4`에서).

- **도구 준비**: 이 기계에는 emsdk, Node, Ninja가 없었다. emsdk 3.1.74를 `~/emsdk`에, Ninja를 pip로 설치했다.
  - Git Bash에서 `source ~/emsdk/emsdk_env.sh`를 읽으면 `EMSDK`와 `PATH`가 잡히지 않았다. 그래서 첫 구성은 `CMake was unable to find a build program corresponding to "Ninja"`와 컴파일러 없음으로 실패했다.
  - PowerShell에서 `emsdk_env.ps1`을 읽으니 잡혔다. 가이드 3절에 적었다.
- **구현**:
  - `tests/host/web/pages/`: 버전 목록 `index.html`, 비교 `compare.html`와 `compare.js`.
  - `scripts/pages_site.mjs`: 조립, `versions.json` 합치기, 루트 갱신 조건, 크기 예산.
  - `serve.mjs --site`: 마운트 목록으로 일반화했다. 기존 `startServer(binDir)`의 동작(`/bin/`은 빌드, `/`는 이 디렉터리)은 그대로다.
  - `drive_pages.mjs`: 목록, 버전 페이지의 `bench-smoke`, 비교와 기대 결과를 확인한다.
  - `.github/workflows/pages.yml`, CI `browsers`의 "Pages site smoke" 단계.
  - 계약 주석: `worker.js`, `src/tools/bench/main.cpp`.
  - 설계에서 바꾼 것:
    - 조립 스크립트를 셸(`pages_site.sh`)이 아니라 Node(`pages_site.mjs`)로 썼다. 이 기계의 Git Bash에는 `jq`가 없고, 같은 스크립트를 Windows와 CI에서 돌리기 위해서다.
    - 루트는 `report.mjs`를 복사하지 않는다. `compare.js`가 판정을 스스로 한다. 버전 확인, `verified=fail`, `engine_available=false`는 #48의 `parseBench`에 없기 때문이다.
- **크기**(설계 "크기 예산"): 버전 디렉터리 하나는 9.4 MB다(`bin/` 9.5 MB 중 `.js`, `.wasm`, `.json`). 500 MB 예산에 50개 남짓이 들어간다.
- **조립 스크립트의 경계 경우**(스크래치 디렉터리):
  - 0.0.23을 루트와 함께 올린 뒤, 0.0.22를 `--root-version 0.0.22`로 올리면 `root_updated=false`이고 루트가 그대로였다.
  - 0.0.23을 `--root-version 0.0.24`로 다시 올리면 루트가 바뀌었다.
  - `--budget-mb 25`로 0.0.24를 올리면 `removed=0.0.22`이고, 올리는 버전은 지우지 않았다.
  - 실행기가 없는 디렉터리(`pages/`)를 `--runner`로 주면 `versions before 0.0.23 have no browser runner`로 거절했다. `--version 1.2`는 `not X.Y.Z`로 거절했다. 둘 다 종료 코드 2다.
- **발견 1**(A와 B가 같은 버전): 처음 판에서는 실행을 버전으로 나눠 A와 B의 표에 같은 실행이 모두 들어갔다. 그래서 B/A가 늘 1.000이었다. 실행을 쪽(A, B)으로 나누도록 고쳤다. 이제 같은 버전끼리의 비교가 기기의 A/A 변동을 보여 준다.
- **발견 2**(A/A 변동, 설계 결정 4): v0.0.23을 자기 자신과 기본 규모(20프레임)로 비교했다. Chromium과 Firefox, 엔진 둘, 워크로드 여섯이다.
  - 3라운드 첫 실행: 24행 가운데 5행이 ±5%를 넘었다. Chromium wasm alu 0.862, memory 0.937, Firefox interpreter memory 0.932, x87 0.923, wasm x87 1.058이다.
  - 그래서 "±5%를 넘고 두 쪽의 실행값 범위가 겹치지 않을 때만 표시"로 바꿨다.
  - 3라운드 둘째 실행(조건 적용): 7행이 ±5%를 넘었고, 그 가운데 6행은 범위가 겹쳐 표시되지 않았다. Chromium interpreter mixed 0.944 한 행이 범위가 갈라져 거짓으로 표시되었다.
  - 변화가 없을 때 쪽마다 n개의 값이 완전히 갈라질 확률은 2/C(2n, n)이다(계산). n = 3이면 10%, n = 5이면 0.8%다. 그래서 기본 라운드를 5로 올리고, 고를 수 있는 범위를 1~7로 넓혔다.
  - 5라운드(기본) 실행: 24행 가운데 4행이 ±5%를 넘었다. Chromium interpreter x87 0.946, mixed 0.947, wasm mixed 1.059, Firefox wasm call 1.082다. 범위가 갈라진 행은 없어서 표시된 행이 0이었다. 두 브라우저의 실행 40번이 175초 걸렸다.
- **검증**:
  - `drive_pages.mjs`, Chromium과 Firefox, `rounds=1&frames=2`:
    - 0.0.23끼리: 목록 `count=2`, 버전 페이지 `[rex86-bench] result=ok`, 비교 `result=ok`.
    - 0.0.1(같은 빌드)과 0.0.23: 0.0.1 쪽 실행이 모두 `the build reports version 0.0.23, not 0.0.1`로 실패하고 `result=fail`이 났다. 기대대로다(`--expect fail`).
  - 기존 `drive.mjs`(일반화한 `startServer`), Chromium, `--only unit,trace,bench-smoke,async`: 넷 모두 `result=ok`. `checks=2021 failures=0`, `cases=18396 mismatches=0`, 비동기 줄은 #48과 같다(`installed=12 failed=0 ran_while_pending=1`).
  - `actionlint .github/workflows/pages.yml .github/workflows/ci.yml`: 보고 없음.
  - 배포 워크플로 자체는 tag push 뒤에 확인한다. 그 결과는 날짜별 절로 더한다.

*Host: AMD Ryzen 5 5600X (Zen 3), 12 threads, Windows 11 Pro, emsdk 3.1.74 (with its Node 24.19.0), Ninja 1.13.2 (pip), Playwright 1.63.0 (headless Chromium 153.0.8010.12, Firefox 155.0), actionlint 1.7.12; branch `work/i052-pages-browser-tests` (from main `8303ee4`). Tooling: emsdk, Node and Ninja were missing and were installed (emsdk at `~/emsdk`, Ninja through pip); sourcing `emsdk_env.sh` in Git Bash set neither `EMSDK` nor `PATH`, so the first configure failed for want of Ninja and a compiler, while `emsdk_env.ps1` in PowerShell worked (now in guide section 3). Implemented `tests/host/web/pages/` (the version list, `compare.html` and `compare.js`), `scripts/pages_site.mjs` (assembly, the `versions.json` merge, the root condition, the size budget), `serve.mjs --site` (generalized to a mount list, `startServer(binDir)` unchanged), `drive_pages.mjs`, `pages.yml`, CI's "Pages site smoke" step and the contract comments in `worker.js` and `src/tools/bench/main.cpp`. Changed from the design: the assembler is Node rather than shell (no `jq` in this Git Bash, one script for Windows and CI), and the root copies no `report.mjs`, `compare.js` judging on its own since #48's `parseBench` knows neither the version check, `verified=fail` nor `engine_available=false`. Size: one version directory is 9.4 MB, some fifty in 500 MB. Assembler edge cases: a backfill of 0.0.22 with `--root-version 0.0.22` left the root alone, 0.0.23 with `--root-version 0.0.24` replaced it, `--budget-mb 25` removed 0.0.22 and never the version being deployed, and a runner directory without the runner or `--version 1.2` were refused with exit code 2. Finding 1: the first version split runs by version, so with A equal to B every run went to both sides and B/A was always 1.000; runs are now split by side, so a version against itself shows the device's A/A variation. Finding 2 (A/A variation, design decision 4): v0.0.23 against itself at the default 20 frames in Chromium and Firefox, two engines, six workloads; at three rounds five of 24 rows passed ±5% (Chromium wasm alu 0.862 and memory 0.937, Firefox interpreter memory 0.932 and x87 0.923, wasm x87 1.058), so marking now needs the sides' ranges not to overlap as well; rerun with it, seven rows passed ±5% and six of them overlapped, while Chromium interpreter mixed (0.944) separated and was marked falsely; complete separation of n values per side with no change has probability 2/C(2n, n) (computed), 10% at three and 0.8% at five, so the default became five rounds with one to seven selectable; at five rounds four rows passed ±5% (Chromium interpreter x87 0.946 and mixed 0.947, wasm mixed 1.059, Firefox wasm call 1.082), none separated and none was marked, the 40 runs over both browsers taking 175 seconds. Verification: `drive_pages.mjs` in Chromium and Firefox at `rounds=1&frames=2` passed the list (`count=2`), the version page (`[rex86-bench] result=ok`) and the comparison of 0.0.23 with itself (`result=ok`), and comparing 0.0.1 (the same build) failed every 0.0.1 run with `the build reports version 0.0.23, not 0.0.1` and `result=fail`, as expected; the existing `drive.mjs` over the generalized `startServer` passed `unit`, `trace`, `bench-smoke` and `async` in Chromium (`checks=2021 failures=0`, `cases=18396 mismatches=0`, the asynchronous line as in #48); actionlint reported nothing for `pages.yml` and `ci.yml`; the deployment workflow itself is checked after the tag push, in a dated section.*

## 2026-10-11 (배포 뒤 / after deployment)

- **배포**: v0.0.24 tag push의 `pages` 실행([38138784549](https://github.com/reexec/rex86/actions/runs/38138784549))이 성공했다. 사용자가 Pages 원본을 `gh-pages`로 정한 뒤, ref `v0.0.24`, `version=0.0.23`의 수동 실행([38142142340](https://github.com/reexec/rex86/actions/runs/38142142340))으로 v0.0.23을 채워 넣었다. `versions.json`에 두 버전이 있다.
- **사용자의 비교 결과**(Windows, Edge 155.0, 기기 미상, A=0.0.23, B=0.0.24, interpreter와 wasm, 5라운드, 기본 프레임): `result=ok`. 12행 가운데 2행이 범위가 갈라졌다.
  - wasm call 1.083(A 101.2~117.9, B 121.8~132.2): 개선 색으로 표시되었다.
  - wasm memory 1.047(A 140.7~144.9, B 146.5~159.6): 범위는 갈라졌지만 5% 아래라 표시되지 않았다.
- **이 표시는 거짓이다**(확인됨):
  - 배포된 두 `rex86_bench.wasm`은 572,239바이트 가운데 17바이트만 다르다. 데이터 섹션에 11바이트(버전 문자열 부근)이고, 코드 섹션에 6바이트(오프셋 269,788~274,325, 코드 섹션 끝의 상수)다. 인터프리터와 번역 실행기의 코드는 같은 바이트다. `.js`는 같다.
  - 배포된 사이트를 헤드리스 Chromium 153(AMD Ryzen 5 5600X, Windows 11)으로 wasm 엔진만 5라운드씩 돌렸다. A=0.0.23/B=0.0.24에서 call 1.018, 순서를 바꾼 A=0.0.24/B=0.0.23에서 0.980, A/A(0.0.23끼리)에서 0.996이었다. 세 실행 모두 범위가 갈라진 행이 18행 가운데 0이었다. 차이는 버전을 따라가지 않았다.
- **의미**: 설계 결정 4의 거짓 표시 확률(5라운드에서 행마다 0.8%)은 두 쪽의 실행을 서로 바꿔도 같은 분포라는 가정의 계산이다. 실제 브라우저 세션에서는 한 행에서 거짓 표시가 났다. 12행에서 기대값은 0.1행이다. 그래서 그 가정이 늘 성립하지는 않는다(추정). 원인 후보는 전력 상태의 변화, 다른 탭과 프로세스, 엔진의 모듈 코드 캐시다. 어느 것인지는 미확정이다.
  - 당장은 가이드 4절의 권고(같은 세션에서 A/A를 함께 돌려 보고 그 행이 흔들리는지 보기)로 읽는다.
  - 비교 페이지가 A/A를 자동으로 함께 돌려 판정에 쓰는 개선은 후속 이슈 후보다.
- 이 결과는 데스크톱이라 기록 5절(실기기)에는 넣지 않는다.

*Deployment: the v0.0.24 tag push's `pages` run (38138784549) succeeded; after the user set the Pages source to `gh-pages`, a manual run on ref `v0.0.24` with `version=0.0.23` (38142142340) backfilled v0.0.23, and `versions.json` lists both. The user's comparison (Windows, Edge 155.0, device unknown, A=0.0.23, B=0.0.24, interpreter and wasm, five rounds, default frames) ended `result=ok` with two of twelve rows separated: wasm call 1.083 (A 101.2 to 117.9, B 121.8 to 132.2), colored as an improvement, and wasm memory 1.047 (A 140.7 to 144.9, B 146.5 to 159.6), separated but under 5% and uncolored. That mark is false (confirmed): the two deployed `rex86_bench.wasm` differ in 17 of 572,239 bytes, 11 in the data section near the version string and 6 in the code section (offsets 269,788 to 274,325, constants at the section's end), the interpreter's and translation runtime's code being byte-identical and the `.js` identical; running the deployed site in headless Chromium 153 (AMD Ryzen 5 5600X, Windows 11), wasm only, five rounds each, gave call 1.018 for A=0.0.23/B=0.0.24, 0.980 with the order reversed and 0.996 for A/A (0.0.23 against itself), no row of 18 separating, so the difference did not follow the version. Meaning: design decision 4's false-mark probability (0.8% a row at five rounds) is computed on the sides' runs being exchangeable, and a real browser session marked one row falsely where 0.1 of twelve rows was expected, so the assumption does not always hold (inferred); candidate causes are power state changes, other tabs and processes and the engine's module code cache, which one unresolved. For now read results per guide section 4's advice (run an A/A in the same session and see whether the row moves); having the comparison page run an A/A itself and use it in the marking is a candidate follow-up issue. Being a desktop, this result stays out of record section 5 (real devices).*
