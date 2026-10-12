# 가이드 : 브라우저 테스트 / Guide : browser tests

근거: [#48 설계](../design/20261011-i048-browser-validation.md), [#52 설계](../design/20261011-i052-pages-browser-tests.md) | 로그: [20261011-i048](../work-logs/20261011-i048-browser-validation.md), [20261011-i052](../work-logs/20261011-i052-pages-browser-tests.md) | 기록: [브라우저의 wasm 번역](../analysis/browser-wasm.md)

`tests/host/web/`의 실행기는 Emscripten 테스트 도구를 실제 브라우저에서 돌립니다. 도구는 Worker 안에서 돌고, 비동기 설치 어댑터의 테스트만 메인 스레드에서 돕니다. 모음은 단위 테스트, IR 차등(wasm 포함), 견고성 smoke, trace 묶음, probe, 벤치마크 smoke, 벤치마크의 세 엔진, 비동기 테스트입니다. CI의 `browsers` 작업은 Playwright로 Chromium, Firefox, WebKit에서 모음을 돌립니다. 실기기(모바일 Chrome, iOS Safari, macOS Safari)에서는 GitHub Pages에 올린 릴리스별 페이지를 엽니다(3절). 같은 기기에서 두 버전의 벤치마크를 비교할 수도 있습니다(4절).

*The runner in `tests/host/web/` runs the Emscripten test tools in real browsers, in Workers, with only the asynchronous installer's test on the main thread. The suite: unit tests, the IR differential (with wasm), robustness smoke, the trace corpus, probe, benchmark smoke, the benchmark's three engines and the asynchronous test. CI's `browsers` job runs it in Chromium, Firefox and WebKit through Playwright; on real devices (mobile Chrome, iOS Safari, macOS Safari) the per-release pages on GitHub Pages open (section 3), and two versions' benchmarks can be compared on one device (section 4).*

## 1. 브라우저용 빌드 / The browser build

`REX86_BROWSER=ON`은 Node 전용 `-sNODERAWFS` 없이, 종료 코드를 페이지에 전하는 `-sEXIT_RUNTIME=1`로 도구를 링크하고, trace 묶음을 `rex86_trace`에 싣습니다. 이 구성은 ctest를 등록하지 않습니다. 비동기 테스트 `rex86_async_test`는 보통 wasm32 구성의 ctest(Node)에도 있습니다.

*`REX86_BROWSER=ON` links the tools without Node's `-sNODERAWFS`, with `-sEXIT_RUNTIME=1` so the exit code reaches the page, and embeds the trace corpus in `rex86_trace`. This configuration registers no ctest; the asynchronous test `rex86_async_test` is also in the ordinary wasm32 configuration's ctest (Node).*

```bash
source ~/emsdk/emsdk_env.sh            # emsdk 3.1.74
cmake --preset web-browser-release && cmake --build --preset web-browser-release
# 또는 / or: emcmake cmake -S . -B build/web-browser-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DREX86_BROWSER=ON
```

빌드 디렉터리의 `bin/`에 도구들과 `rex86_browser.json`(실린 trace 목록)이 생깁니다.

*The build's `bin/` holds the tools and `rex86_browser.json` (the embedded trace list).*

## 2. Playwright로 돌리기 / Running through Playwright

```bash
cd tests/host/web
npm ci                                              # Playwright 1.63.0 (package-lock.json)
npx playwright install chromium firefox webkit      # Linux CI: --with-deps
node drive.mjs --bin ../../../build/web-browser-release/bin
node drive.mjs --bin ../../../build/web-browser-release/bin --browsers firefox --only unit,irdiff --settle 8000
node drive.mjs --bin ../../../build/web-browser-release/bin --module-limit
```

* 결과는 `[rex86-browser] browser=<이름> test=<테스트> thread=worker|main result=ok|fail exit= ms= retries= summary="..."` 줄입니다. 벤치마크는 워크로드마다 `bench test= workload= mips=` 줄을 더하고, 끝에 `tests= failed= result=`와 전체 `browsers= result=` 줄이 옵니다. 하나라도 실패하면 종료 코드 1입니다.
* 실패한 테스트는 출력의 마지막 40줄을 함께 보여 줍니다.
* `GITHUB_STEP_SUMMARY`가 있으면 브라우저마다 표를 작업 요약에 씁니다. CI 러너의 MIPS는 변동이 커서 기록하지 않습니다([벤치마크 가이드](benchmark.md)).
* `retries=`는 메모리 부족만으로 실패한 실행을 다시 시작한 횟수입니다. 메모리 부족 줄이 있고, 불일치가 설치 거절뿐인 경우입니다. Firefox는 wasm 코드 메모리를 쓰레기 수집에서만 돌려주고 프로세스당 모듈 1만 6천 개 남짓을 넘기지 못합니다. #49부터 도구들이 모듈 하나에 블록 16개를 묶어 Firefox도 대기 없이 통과하지만, 모음은 테스트 사이에 1초를 기다리고, 이런 실패는 15초 뒤 두 번까지 다시 시작합니다([기록](../analysis/browser-wasm.md) 3절). 도구는 시드가 고정이므로, 다시 시작한 실행이 통과하면 거절 뒤에 숨은 불일치가 없다는 뜻입니다. `retries`가 0이 아니면 결과 줄과 함께 적습니다. `--settle ms`(페이지에서는 `?settle=ms`)로 대기를 바꿉니다.

* `--module-limit`은 모음 대신 모듈 한도 탐침(`module_limit.html`)을 브라우저마다 새 프로세스에서 한 번 돌립니다. 함수 하나짜리 작은 모듈을 4만 개까지 만들고 `[rex86-browser] browser= module_limit funcs=1 cap=40000 modules= ended=cap|refused error=` 줄을 냅니다. 탐침이 돌지 못했을 때만 실패합니다. Firefox는 `modules=16349 ended=refused`입니다([기록](../analysis/browser-wasm.md) 3절, [#49](https://github.com/reexec/rex86/issues/49)).

*`--module-limit` runs the module limit probe (`module_limit.html`) instead of the suite, once per browser in a fresh process: it makes up to 40,000 small one-function modules and prints `[rex86-browser] browser= module_limit funcs=1 cap=40000 modules= ended=cap|refused error=`, failing only when the probe could not run; Firefox gives `modules=16349 ended=refused` (record section 3, #49).*

*Results are `[rex86-browser] browser=<name> test=<test> thread=worker|main result=ok|fail exit= ms= retries= summary="..."` lines; the benchmark adds a `bench test= workload= mips=` line per workload, and `tests= failed= result=` and an overall `browsers= result=` line close the report, the exit code being 1 if anything failed. A failed test shows its last 40 output lines. With `GITHUB_STEP_SUMMARY` set, a table per browser goes to the job summary; CI runners' MIPS vary too much to record (benchmark guide). `retries=` counts restarts of a run that failed for want of memory alone (an out-of-memory line, no mismatch other than a refused installation): Firefox returns wasm code memory only at a garbage collection and cannot exceed some 16,000 modules per process, so, although since #49 the tools install 16 blocks per module and Firefox passes with no wait, the suite waits 1 second between tests and restarts such a failure up to twice after 15 seconds (record, section 3); the tools' seeds being fixed, a restarted run that passes shows no mismatch was hiding behind a refusal. Note a nonzero `retries` along with the result line; `--settle ms` (`?settle=ms` on the page) changes the wait.*

## 3. 실기기에서 돌리기 / Running on a real device

릴리스된 버전은 GitHub Pages에 있습니다([#52](https://github.com/reexec/rex86/issues/52)). 빌드도 서버도 필요 없습니다.

*Released versions are on GitHub Pages (#52); neither a build nor a server is needed.*

1. 기기의 브라우저로 **<https://reexec.github.io/rex86/>** 를 엽니다. 버전 목록이 새 버전부터 보입니다.
2. 버전 행의 링크를 고릅니다.
   * **Benchmark**: 인터프리터와 wasm 번역의 벤치마크만 돕니다. 휴대폰의 기본 경로입니다.
   * **Full suite**: #48 모음 전체입니다. 단위 테스트, IR 차등, 견고성, trace, probe가 더해져 훨씬 오래 걸립니다.
3. 끝날 때까지 화면을 켜 두고 탭을 앞에 둡니다. 메인 스레드 테스트는 프레임 루프(`requestAnimationFrame`)로 돌기 때문에, 탭이 백그라운드로 가면 멈춥니다.
4. 표가 다 차면(상태 줄이 `all N tests passed` 또는 `N failed`) **Copy report**로 보고 줄을 복사합니다. 복사가 막힌 브라우저에서는 보고 칸을 길게 눌러 선택합니다.
5. 보고 줄과 기기 정보(모델, OS 버전, 브라우저 버전, 전원 연결 여부)를 [기록](../analysis/browser-wasm.md)의 실기기 절에 더합니다. 벤치마크 수치는 같은 기기에서 세 번 돌린 중앙값을 적습니다. 페이지를 다시 읽으면 처음부터 다시 돕니다.

6. 모듈 한도: 같은 버전 디렉터리의 `module_limit.html`(예: `https://reexec.github.io/rex86/v0.0.28/module_limit.html`, 이 탐침이 들어간 버전부터)을 **새로 연 브라우저**에서 엽니다. 앞서 만든 모듈도 한도에 들어가므로, 모음을 돌린 탭 뒤가 아니라 브라우저를 다시 시작한 뒤에 엽니다. 끝나면 `[rex86-module-limit]` 줄을 기록 3절에 더합니다. Safari(JavaScriptCore)의 한도는 이것으로 잽니다.

*1 Open **https://reexec.github.io/rex86/** in the device's browser; the version list starts from the newest. 2 Pick a link in a version's row: **Benchmark** runs the interpreter and wasm-translation benchmark alone, the default path on phones; **Full suite** runs #48's whole suite, adding the unit tests, IR differential, robustness, traces and probe, and takes much longer. 3 Keep the screen on and the tab in front until it finishes: the main-thread test runs on the frame loop (`requestAnimationFrame`) and stops in a background tab. 4 When the table is complete (the status line reads `all N tests passed` or `N failed`), copy the report lines with **Copy report**, or long-press the report box where copying is blocked. 5 Add the report lines and the device details (model, OS version, browser version, whether on power) to the record's real-device section; for benchmark figures record the median of three runs on the same device. Reloading the page starts over. 6 The module limit: open `module_limit.html` in the same version directory (for example `https://reexec.github.io/rex86/v0.0.28/module_limit.html`, from the version that carries the probe) in a **freshly started browser**, since modules made earlier count toward the limit, and add its `[rex86-module-limit]` line to the record's section 3; this is how Safari's (JavaScriptCore's) limit is measured.*

### 아직 릴리스되지 않은 빌드 / A build not released yet

브랜치의 빌드를 기기에서 보려면 개발 기계에서 브라우저용 빌드를 만들고(1절) 정적 서버를 LAN에 띄웁니다. 서버는 이 디렉터리를 `/`에, 빌드의 `bin/`을 `/bin/`에 내주고, 접속할 LAN 주소를 출력합니다. 그 주소를 기기에서 열고 위 3~5단계를 따릅니다. `?only=bench-interpreter,bench-wasm`을 붙이면 벤치마크만 돕니다.

*To see a branch's build on a device, build for browsers on a development machine (section 1) and start the static server on the LAN; it serves this directory at `/` and the build's `bin/` at `/bin/` and prints the LAN addresses. Open one on the device and follow steps 3 to 5 above; `?only=bench-interpreter,bench-wasm` runs the benchmark alone.*

```bash
node tests/host/web/serve.mjs build/web-browser-release/bin 8048
# [rex86-serve] open http://192.168.0.10:8048/
```

서버는 읽기 전용이고 캐시를 끕니다. 믿을 수 있는 LAN에서만 띄웁니다. 방화벽이 포트를 막으면 엽니다. Windows에서는 PowerShell에서 emsdk의 `emsdk_env.ps1`을 읽어야 `node`와 `emcmake`가 잡힙니다. Git Bash에서 `emsdk_env.sh`를 읽어서는 잡히지 않았습니다([#52 로그](../work-logs/20261011-i052-pages-browser-tests.md)).

*The server is read-only with caching off, to be run on a trusted LAN only, the firewall opened for the port if needed. On Windows, source emsdk's `emsdk_env.ps1` in PowerShell to get `node` and `emcmake`; sourcing `emsdk_env.sh` in Git Bash did not set them (#52 log).*

## 4. 두 버전 비교 / Comparing two versions

MIPS는 기기마다 다릅니다. 그래서 버전 사이의 개선은 같은 기기에서 두 버전을 함께 재야 보입니다([#52 설계](../design/20261011-i052-pages-browser-tests.md) 결정 4).

*MIPS differ by device, so an improvement between versions shows only when both are measured on one device (#52 design, decision 4).*

1. 버전 목록 위의 **Compare two versions**를 엽니다. 기본값은 바로 아래 버전(A)과 최신 버전(B)입니다.
2. 엔진(기본: interpreter와 wasm)과 라운드 수(기본 5)를 고르고 **Start**를 누릅니다. 두 버전을 번갈아 돌리고, 라운드마다 앞서는 버전을 바꿉니다(A→B, B→A). 기본 설정이면 실행 20번입니다. 라운드를 줄이면 빨리 끝나지만, 변화가 없어도 색이 칠해질 확률이 커집니다(3라운드면 행마다 10%, 5라운드면 0.8%).
3. 엔진마다 표가 나옵니다. 열은 A와 B의 중앙값 MIPS와 B/A입니다.
   * B/A가 1.05 이상이거나 0.95 이하이고, A와 B의 실행값이 겹치지 않을 때만 색이 칠해집니다(초록은 개선, 빨강은 후퇴).
   * 행에 마우스를 올리거나 길게 누르면 각 실행의 값이 보입니다.
4. **이 기기의 변동부터 봅니다.** 같은 버전을 A와 B에 두고 한 번 돌리면, 변화가 없을 때 B/A가 얼마나 흔들리는지 보입니다. 데스크톱(AMD Ryzen 5 5600X)에서도 같은 빌드끼리 중앙값이 최대 14% 달랐습니다(Chromium wasm alu 0.862, [#52 로그](../work-logs/20261011-i052-pages-browser-tests.md)). 휴대폰은 발열 때문에 더 흔들릴 수 있습니다.
5. **Copy report**의 `[rex86-compare]` 줄을 기기 정보와 함께 기록 5절이나 이슈에 붙입니다. `result=fail`이면 **Runs**의 접힌 출력에서 이유를 봅니다. 고른 버전과 빌드가 보고한 `version=`이 다르면 캐시에 남은 옛 배포일 수 있으니, 페이지를 새로 고칩니다.

*1 Open **Compare two versions** above the version list; it defaults to the version below the newest (A) and the newest (B). 2 Pick the engines (default interpreter and wasm) and rounds (default 5) and press **Start**; the versions alternate, the one going first swapping each round (A then B, B then A), twenty runs at the defaults; fewer rounds finish sooner but raise the chance of a color with no change (10% a row at three rounds, 0.8% at five). 3 A table per engine gives A's and B's median MIPS and B/A, colored (green better, red worse) only at 1.05 or more or 0.95 or less when no run of A overlaps a run of B; hovering over or long-pressing a row shows each run. 4 **See this device's variation first**: the same version as A and B shows how far B/A moves with no change; even a desktop (AMD Ryzen 5 5600X) saw one build's medians differ by up to 14% (Chromium wasm alu 0.862, #52 log), and a phone may move more as it heats. 5 Paste the `[rex86-compare]` lines from **Copy report** with the device details into record section 5 or the issue. On `result=fail`, read the reason in the collapsed output under **Runs**; a chosen version differing from the build's `version=` may be an earlier deployment left in a cache, so reload the page.*

### 배포 / Deployment

`v*` tag를 push하면 워크플로 `pages.yml`이 그 tag를 빌드해 `/vX.Y.Z/`에 더합니다. 루트(목록과 비교)는 배포된 최고 버전의 tag에서만 갱신됩니다.

* **처음 한 번**: 저장소 **Settings → Pages**에서 원본을 `gh-pages` 브랜치의 `/ (root)`로 정합니다.
* **이미 있는 릴리스 채워 넣기**: Actions의 **pages** → **Run workflow**에서 ref를 최신 tag로 고르고, `version`에 그 릴리스(예: `0.0.23`)를 넣습니다. v0.0.23 미만은 브라우저 실행기가 없어 거절됩니다.
* **크기**: 작업 요약에 남습니다. 버전 하나가 약 9.4 MB입니다. 합이 500 MB를 넘으면 가장 오래된 버전부터 빠집니다.

*Pushing a `v*` tag makes the `pages.yml` workflow build that tag and add it at `/vX.Y.Z/`; the root (list and comparison) is refreshed only from the highest deployed version's tag. The first time, set the source to the `gh-pages` branch's `/ (root)` in the repository's **Settings → Pages**. To backfill a release, run **pages** from Actions with **Run workflow**, choosing the newest tag as the ref and the release (such as `0.0.23`) as `version`; releases before 0.0.23 have no browser runner and are refused. The job summary records the size: some 9.4 MB per version, the oldest dropped once the total passes 500 MB.*

### 로컬에서 사이트 확인 / Checking the site locally

```bash
version=$(cat VERSION)
node scripts/pages_site.mjs --site build/pages-site --version "$version" --bin build/web-browser-release/bin \
  --runner tests/host/web --root tests/host/web/pages --root-version "$version"
node scripts/pages_site.mjs --site build/pages-site --version 0.0.1 --bin build/web-browser-release/bin --runner tests/host/web
cd tests/host/web
node drive_pages.mjs --site ../../../build/pages-site --compare "$version,$version" --expect ok --browsers chromium,firefox
node drive_pages.mjs --site ../../../build/pages-site --compare "0.0.1,$version" --expect fail
node serve.mjs --site ../../../build/pages-site 8048     # 사람이 열어 볼 때 / to open it by hand
```

같은 빌드를 제 버전과 `0.0.1`(빌드가 보고하지 않는 버전) 두 곳에 조립합니다. 첫 비교는 통과해야 하고, 둘째는 버전 확인 때문에 실패해야 합니다. CI `browsers` 작업이 Chromium에서 같은 확인을 돌립니다. `--rounds 3 --frames default`를 주면 기본 규모로 돌아, 그 기계의 A/A 변동을 잴 수 있습니다.

*One build is assembled under its own version and under `0.0.1`, a version it does not report; the first comparison must pass and the second fail for the version check. CI's `browsers` job runs the same checks in Chromium. `--rounds 3 --frames default` runs at the default scale and measures the machine's A/A variation.*

## 5. 실패를 읽는 법 / Reading a failure

* `abort: ... out of memory`이고 `retries=2`: 메모리 회수가 따라오지 못했습니다. `?settle=10000`으로 테스트 사이 대기를 늘려 다시 돌립니다. 그래도 실패하면 기록 3절의 모듈 수 한도를 봅니다.
* irdiff의 `first mismatch: ... the host refused the module`: 생성 모듈 설치가 거절됐습니다(번역 결과가 틀린 것은 아닙니다). 바로 위의 `rex86: a generated module failed to install:` 줄이 이유를 말합니다. 코어 자체는 거절된 블록을 인터프리터로 돌리지만, irdiff는 설치를 요구하므로 불일치로 셉니다.
* 그 밖의 불일치(`mismatches=` 0이 아님, `verified=fail`)는 코어나 백엔드의 결함 후보입니다. 같은 인자를 Node(`node build/web-wasm-release/bin/<도구>.js ...`)에서 돌려 엔진 차이인지 가립니다.

*`abort: ... out of memory` with `retries=2`: reclamation did not keep up; rerun with `?settle=10000` for a longer wait between tests, and if it still fails see section 3 of the record on the module count limit. irdiff's `first mismatch: ... the host refused the module`: installing a generated module was refused, not a wrong translation; the `rex86: a generated module failed to install:` line just above gives the reason; the core itself runs a refused block on the interpreter, but irdiff demands installation and counts it as a mismatch. Any other mismatch (nonzero `mismatches=`, `verified=fail`) is a candidate defect in the core or the backend; run the same arguments on Node to tell whether the engine differs.*
