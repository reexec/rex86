# 가이드 : 브라우저 테스트 / Guide : browser tests

근거: [#48 설계](../design/20261011-i048-browser-validation.md) | 로그: [20261011-i048](../work-logs/20261011-i048-browser-validation.md) | 기록: [브라우저의 wasm 번역](../analysis/browser-wasm.md)

`tests/host/web/`의 실행기는 Emscripten 테스트 도구를 실제 브라우저에서 돌립니다. 도구는 Worker 안에서 돌고, 비동기 설치 어댑터의 테스트만 메인 스레드에서 돕니다. 모음은 단위 테스트, IR 차등(wasm 포함), 견고성 smoke, trace 묶음, probe, 벤치마크 smoke, 벤치마크의 세 엔진, 비동기 테스트입니다. CI의 `browsers` 작업은 Playwright로 Chromium, Firefox, WebKit에서 모음을 돌립니다. 실기기(모바일 Chrome, iOS Safari, macOS Safari)에서는 같은 페이지를 LAN으로 엽니다.

*The runner in `tests/host/web/` runs the Emscripten test tools in real browsers, in Workers, with only the asynchronous installer's test on the main thread. The suite: unit tests, the IR differential (with wasm), robustness smoke, the trace corpus, probe, benchmark smoke, the benchmark's three engines and the asynchronous test. CI's `browsers` job runs it in Chromium, Firefox and WebKit through Playwright; on real devices (mobile Chrome, iOS Safari, macOS Safari) the same page opens over the LAN.*

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
```

* 결과는 `[rex86-browser] browser=<이름> test=<테스트> thread=worker|main result=ok|fail exit= ms= retries= summary="..."` 줄입니다. 벤치마크는 워크로드마다 `bench test= workload= mips=` 줄을 더하고, 끝에 `tests= failed= result=`와 전체 `browsers= result=` 줄이 옵니다. 하나라도 실패하면 종료 코드 1입니다.
* 실패한 테스트는 출력의 마지막 40줄을 함께 보여 줍니다.
* `GITHUB_STEP_SUMMARY`가 있으면 브라우저마다 표를 작업 요약에 씁니다. CI 러너의 MIPS는 변동이 커서 기록하지 않습니다([벤치마크 가이드](benchmark.md)).
* `retries=`는 메모리 부족만으로 실패한 실행을 다시 시작한 횟수입니다. 메모리 부족 줄이 있고, 불일치가 설치 거절뿐인 경우입니다. Firefox는 wasm 코드 메모리를 쓰레기 수집에서만 돌려주고 프로세스당 모듈 1만 6천 개 남짓을 넘기지 못합니다. 그래서 모음은 테스트 사이에 5초를 기다리고, 이런 실패는 15초 뒤 두 번까지 다시 시작합니다([기록](../analysis/browser-wasm.md) 3절). 도구는 시드가 고정이므로, 다시 시작한 실행이 통과하면 거절 뒤에 숨은 불일치가 없다는 뜻입니다. `retries`가 0이 아니면 결과 줄과 함께 적습니다. `--settle ms`(페이지에서는 `?settle=ms`)로 대기를 바꿉니다.

*Results are `[rex86-browser] browser=<name> test=<test> thread=worker|main result=ok|fail exit= ms= retries= summary="..."` lines; the benchmark adds a `bench test= workload= mips=` line per workload, and `tests= failed= result=` and an overall `browsers= result=` line close the report, the exit code being 1 if anything failed. A failed test shows its last 40 output lines. With `GITHUB_STEP_SUMMARY` set, a table per browser goes to the job summary; CI runners' MIPS vary too much to record (benchmark guide). `retries=` counts restarts of a run that failed for want of memory alone (an out-of-memory line, no mismatch other than a refused installation): Firefox returns wasm code memory only at a garbage collection and cannot exceed some 16,000 modules per process, so the suite waits 5 seconds between tests and restarts such a failure up to twice after 15 seconds (record, section 3); the tools' seeds being fixed, a restarted run that passes shows no mismatch was hiding behind a refusal. Note a nonzero `retries` along with the result line; `--settle ms` (`?settle=ms` on the page) changes the wait.*

## 3. 실기기에서 돌리기 / Running on a real device

1. 개발 기계에서 브라우저용 빌드를 만들고(1절) 정적 서버를 띄웁니다. 서버는 이 디렉터리를 `/`에, 빌드의 `bin/`을 `/bin/`에 내주고, 접속할 LAN 주소를 출력합니다.

   ```bash
   node tests/host/web/serve.mjs build/web-browser-release/bin 8048
   # [rex86-serve] open http://192.168.0.10:8048/
   ```

   서버는 읽기 전용이고 캐시를 끕니다. 믿을 수 있는 LAN에서만 띄웁니다. 방화벽이 포트를 막으면 엽니다.

2. 기기를 같은 네트워크에 두고, 출력된 주소를 기기의 브라우저로 엽니다. 화면을 켜 둡니다. 메인 스레드 테스트는 프레임 루프(`requestAnimationFrame`)로 돌기 때문에 탭이 백그라운드로 가면 멈춥니다.
3. 표가 다 차면(상태 줄이 `all N tests passed` 또는 `N failed`) **Copy report**로 보고 줄을 복사합니다. 복사가 막힌 브라우저에서는 보고 칸을 길게 눌러 선택합니다.
4. 보고 줄과 기기 정보(모델, OS 버전, 브라우저 버전, 전원 연결 여부)를 [기록](../analysis/browser-wasm.md)의 실기기 절에 더합니다. 벤치마크 수치는 같은 기기에서 세 번 돌린 중앙값을 적습니다. 페이지를 다시 읽으면 처음부터 다시 돕니다. `?only=bench-interpreter,bench-wasm`으로 벤치마크만 돌릴 수 있습니다.

*1. Build for browsers on a development machine (section 1) and start the static server, which serves this directory at `/` and the build's `bin/` at `/bin/` and prints the LAN addresses to open; it is read-only with caching off, to be run on a trusted LAN only, the firewall opened for the port if needed. 2. Put the device on the same network and open the printed address in its browser, keeping the screen on: the main-thread test runs on the frame loop (`requestAnimationFrame`) and stops while the tab is in the background. 3. When the table is complete (the status line reads `all N tests passed` or `N failed`), copy the report lines with **Copy report**, or long-press the report box to select it where copying is blocked. 4. Add the report lines and the device details (model, OS version, browser version, whether on power) to the record's real-device section; for benchmark figures record the median of three runs on the same device. Reloading the page starts over, and `?only=bench-interpreter,bench-wasm` runs the benchmark alone.*

## 4. 실패를 읽는 법 / Reading a failure

* `abort: ... out of memory`이고 `retries=2`: 메모리 회수가 따라오지 못했습니다. `?settle=10000`으로 테스트 사이 대기를 늘려 다시 돌립니다. 그래도 실패하면 기록 3절의 모듈 수 한도를 봅니다.
* irdiff의 `first mismatch: ... the host refused the module`: 생성 모듈 설치가 거절됐습니다(번역 결과가 틀린 것은 아닙니다). 바로 위의 `rex86: a generated module failed to install:` 줄이 이유를 말합니다. 코어 자체는 거절된 블록을 인터프리터로 돌리지만, irdiff는 설치를 요구하므로 불일치로 셉니다.
* 그 밖의 불일치(`mismatches=` 0이 아님, `verified=fail`)는 코어나 백엔드의 결함 후보입니다. 같은 인자를 Node(`node build/web-wasm-release/bin/<도구>.js ...`)에서 돌려 엔진 차이인지 가립니다.

*`abort: ... out of memory` with `retries=2`: reclamation did not keep up; rerun with `?settle=10000` for a longer wait between tests, and if it still fails see section 3 of the record on the module count limit. irdiff's `first mismatch: ... the host refused the module`: installing a generated module was refused, not a wrong translation; the `rex86: a generated module failed to install:` line just above gives the reason; the core itself runs a refused block on the interpreter, but irdiff demands installation and counts it as a mismatch. Any other mismatch (nonzero `mismatches=`, `verified=fail`) is a candidate defect in the core or the backend; run the same arguments on Node to tell whether the engine differs.*
