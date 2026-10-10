# 브라우저의 wasm 번역 / Wasm translation in browsers

근거: [#48 설계](../design/20261011-i048-browser-validation.md), [로그](../work-logs/20261011-i048-browser-validation.md) | 절차: [브라우저 테스트 가이드](../guides/browser-tests.md) | 배경: [kb: wasm에서 실행 중에 만든 코드](../kb/wasm-runtime-code.md)

Emscripten 테스트 도구를 실제 브라우저에서 돌린 결과다. 대상은 단위 테스트, IR 차등(wasm 포함), 견고성 smoke, trace 묶음, probe, 벤치마크, 비동기 설치 어댑터다. 브라우저마다 wasm 엔진이 다르므로(Chromium의 V8, Firefox의 SpiderMonkey, Safari의 JavaScriptCore), Node(V8)에서의 검증(#45)을 엔진별로 넓힌다.

*Results of running the Emscripten test tools in real browsers: unit tests, the IR differential (with wasm), robustness smoke, the trace corpus, probe, the benchmark and the asynchronous installer. Browsers differ in wasm engines (V8 in Chromium, SpiderMonkey in Firefox, JavaScriptCore in Safari), so this widens the Node (V8) verification of #45 engine by engine.*

## 1. 모음 결과 / Suite results

호스트 D(AMD Ryzen 5 5600X, Linux x64), emsdk 3.1.74의 `REX86_BROWSER` Release 빌드, Playwright 1.63.0의 헤드리스 브라우저, 2026-10-11.

*Host D (AMD Ryzen 5 5600X, Linux x64), emsdk 3.1.74's `REX86_BROWSER` Release build, Playwright 1.63.0's headless browsers, 2026-10-11.*

| 브라우저 | 엔진 | 모음 | 비고 |
|---|---|---|---|
| Chromium 153.0.8010.12 | V8 | **확인됨**: 10/10, 다섯 번 실행 모두 통과 | |
| Firefox 155.0 | SpiderMonkey | **확인됨**: 10/10, 대기 5초로 여섯 번 실행 모두 통과, 재시도 0 | 대기 없이 돌리면 실패한다(3절) |
| WebKit 26.6(Playwright 1.63.0) | JavaScriptCore | **확인됨**(CI `browsers`, ubuntu-latest 러너, [실행 38067728830](https://github.com/reexec/rex86/actions/runs/38067728830)): 10/10, 재시도 0 | 이 기계에서는 시스템 라이브러리가 없어 뜨지 않는다. 비동기 테스트는 `installed=12 failed=0 ran_while_pending=1`. `block_runs`는 설치가 도착한 프레임에 따라 다르다(570,550) |
| 실기기(모바일 Chrome, iOS Safari, macOS Safari) | | **미확정**: 가이드 3절의 절차로 사용자가 돌린 뒤 5절에 더한다 | |

* 테스트별 결과는 Node와 같다. 단위 테스트는 `checks=2021 failures=0`, trace 묶음은 `cases=18396 mismatches=0`, IR 차등 5,000건(wasm 실행 포함)은 불일치 0, 견고성 300건은 위반 0이다.
* 비동기 어댑터 테스트(메인 스레드, 2절)는 두 브라우저 모두 `guests=2 frames=61 installed=12 failed=0 ran_while_pending=1 result=ok`로, Node와 같은 줄을 냈다.

*Chromium 153 (V8): confirmed, 10/10, all five runs passing. Firefox 155 (SpiderMonkey): confirmed, 10/10, all six runs with a 5-second wait passing, zero retries; without the wait it fails (section 3). WebKit 26.6 (JavaScriptCore): confirmed in CI's `browsers` job (an ubuntu-latest runner, run 38067728830), 10/10 with zero retries; it does not start on this machine for missing system libraries; its asynchronous test printed `installed=12 failed=0 ran_while_pending=1`, `block_runs` depending on the frame the installations arrived in (570,550). Real devices: unresolved until the user runs the guide's section 3 procedure, recorded in section 5. Per-test results equal Node's: unit tests `checks=2021 failures=0`, the trace corpus `cases=18396 mismatches=0`, 5,000 IR differential cases (with wasm runs) without a mismatch, 300 robustness cases without a violation. The asynchronous adapter's test (main thread, section 2) printed the same line as Node in both browsers, `guests=2 frames=61 installed=12 failed=0 ran_while_pending=1 result=ok`.*

## 2. 비동기 설치 어댑터 / The asynchronous installer

**확인됨**(Node 24.19, Chromium 153, Firefox 155의 메인 스레드): `AsyncWebWasmModuleServices`는 `Install`에서 `kPending`을 돌려주고, 인터프리터가 블록을 계속 돈다. 설치가 끝나면 다음 `Poll`에서 번역 블록이 돈다. `Cpu` 두 개가 코드를 서로 다른 주소에 두고 같은 프레임 루프에서 함께 돌며, 각 결과가 인터프리터만 쓴 실행과 같다.

* 결과 큐를 어댑터 전체가 하나로 쓰면, 한 어댑터의 `Poll`이 다른 `Cpu`의 결과를 가져간다. 큐를 어댑터별로 나누기 전 판에서 이 결함을 직접 넣어 보았고, 테스트가 `result=fail`로 잡았다(로그).
* 끝나지 않은 설치는 어댑터의 소멸자가 버리고, 이미 끝난 결과의 테이블 칸을 돌려준다.

*Confirmed (on Node 24.19 and the main threads of Chromium 153 and Firefox 155): `AsyncWebWasmModuleServices` answers `Install` with `kPending` while the interpreter keeps running the block, and translated blocks run from the `Poll` after installation finishes; two `Cpu`s with code at different addresses run together on one frame loop, each result equal to an interpreter-only run. With one result queue shared by all adapters, one adapter's `Poll` takes another `Cpu`'s results; that defect, put in on purpose, failed the test (`result=fail`, log). The adapter's destructor abandons unfinished installations and gives back the table slots of finished ones.*

## 3. Firefox의 모듈 수 한도 / Firefox's module count limit

**확인됨**(Firefox 155, Linux x64, Worker): 함수 하나짜리 작은 모듈을 `new WebAssembly.Module`과 `new WebAssembly.Instance`로 계속 만들면, **16,349번째에서 `InternalError: out of memory`**가 나고 콘솔에 `failed to allocate executable memory for module` 경고가 남는다. 이 수는 다음 세 경우 모두 같았다.

* 인스턴스를 쥐고 있을 때
* 바로 버릴 때
* 100개 또는 1,000개마다 16 ms씩 이벤트 루프에 제어를 돌려줄 때

같은 실험에서 Chromium 153은 10만 개를 1.5초에 만들었고 실패하지 않았다.

*Confirmed (Firefox 155, Linux x64, a Worker): creating one-function modules with `new WebAssembly.Module` and `new WebAssembly.Instance` fails at the **16,349th with `InternalError: out of memory`**, the console warning `failed to allocate executable memory for module`. The count is the same whether the instances are held, dropped at once, or the loop yields to the event loop for 16 ms every 100 or 1,000 modules. In the same experiment Chromium 153 made 100,000 in 1.5 seconds without failing.*

* **추정**: 버린 모듈의 실행 코드 메모리는 쓰레기 수집에서만 돌아온다. 그런데 코드 메모리 부족은 수집을 일으키지 않는다. 그래서 JS 힙 할당이 수집을 일으키지 않는 한, 프로세스의 실행 코드 예산이 차면 생성이 실패한다. 16,349라는 수는 프로세스당 실행 메모리 예산을 모듈당 고정 단위로 나눈 값으로 보인다. 예산과 단위의 실제 값은 미확정이다. SpiderMonkey 소스(`ProcessExecutableMemory`)에서 확인한다.
* **확인됨**: rex86의 도구에서도 같은 일이 생긴다.
  * `rex86_irdiff --cases 200000`(생성 모듈 약 25만 개)은 Firefox에서 설치 거절 9건을 냈다. irdiff는 거절을 불일치로 세므로 `mismatches=` 합 9와 같다. 번역 결과가 틀린 경우는 없었다.
  * 이 도구는 모듈 바이트를 JS로 복사하며 쓰레기를 만들고, 그 쓰레기가 대부분의 경우 수집을 일으킨다. 그래서 거절이 드물다.
  * 끝난 Worker의 모듈은 바로 회수되지 않는다. 단위 테스트(생성 모듈 수천 개) 바로 뒤에 시작한 도구는 주 `.wasm` 인스턴스화부터 실패했다. 테스트 사이에 3초 이상 기다리면 통과했다.
* **모음의 대응**: 테스트 사이에 5초를 기다린다. 브라우저 모음의 IR 차등은 5,000건으로 줄였다. Node ctest는 2만 건, 릴리스 캠페인은 그보다 큰 규모를 유지한다. 메모리 부족만으로 실패한 실행은 15초 뒤 두 번까지 다시 시작하고, 재시도 횟수는 보고에 남는다. 대기 없이 돌리면 이 재시도로도 회복하지 못했다. 실패한 실행이 모듈을 더 남기기 때문이다.
* **소비자 영향(추정)**: 설치가 거절되면 코어는 그 블록을 인터프리터로 돌리므로 결과는 맞다(#45 계약). 하지만 블록 하나에 모듈 하나를 쓰는 지금의 백엔드는 Firefox에서 프로세스당 1만 6천 개 남짓의 모듈로 묶인다. 번역 실행기의 크기 한도(65,536)보다 훨씬 작다. 무효화 뒤의 재번역도 수집 전까지 이 수에 더해진다. 큰 게스트나 오래 도는 게스트는 번역을 잃을 수 있다. 여러 블록을 모듈 하나로 묶는 설치(#45 결정 2가 남긴 최적화)와 모듈 수 예산이 근거를 얻었다. 후속 이슈로 다룬다.

*Inferred: a dropped module's executable code memory returns only at a garbage collection, which running short of code memory does not start, so creation fails once the process's executable budget is full unless JS heap allocation triggers a collection; 16,349 looks like a per-process executable budget divided by a fixed per-module granule, the actual figures being unresolved (to be checked in SpiderMonkey's `ProcessExecutableMemory`). Confirmed for rex86's tools: `rex86_irdiff --cases 200000` (about 250,000 generated modules) had 9 installations refused in Firefox, matching its `mismatches=` total of 9, irdiff counting refusals as mismatches, with no wrong translation; the tool's copies of module bytes into JS make garbage that usually triggers collections, hence the rarity; a finished Worker's modules are not reclaimed at once, so a tool started right after the unit tests (thousands of generated modules) failed from instantiating its main `.wasm`, and passed after a wait of 3 seconds or more. The suite waits 5 seconds between tests, runs 5,000 IR differential cases in browsers (Node's ctest keeps 20,000 and the release campaign more), and restarts a run that failed for want of memory alone up to twice after 15 seconds, counting the retries in the report; with no wait, those retries did not recover either, the failed runs leaving more modules behind. Consumer impact (inferred): a refused installation leaves the block to the interpreter, so results stay correct (#45's contract), but the current one-module-per-block backend is bounded in Firefox by some 16,000 modules per process, far below the runtime's size limit (65,536), and re-translations after invalidation add to the count until a collection; a large or long-running guest can lose translation. Installing several blocks per module (an optimization #45 decision 2 left open) and a module count budget gain their grounds, for a follow-up issue.*

## 4. 처리량 / Throughput

**확인됨**: 호스트 D, 위 빌드, Worker 안의 `rex86_bench`(20프레임, 기본 설정)다. 브라우저를 번갈아 세 번 돌린 중앙값이고, 단위는 MIPS다. Node 열은 [인터프리터 성능](interpreter-performance.md) 2.4절의 값이다(Node 24.19, 같은 기계, 같은 방법).

*Confirmed: host D, the build above, `rex86_bench` in a Worker (20 frames, defaults), medians of three interleaved runs in MIPS; the Node columns come from section 2.4 of the interpreter performance analysis (Node 24.19, the same machine and method).*

| 워크로드 | Chromium 인터프리터 | 평가기 | wasm | wasm/인터프리터 | Firefox 인터프리터 | 평가기 | wasm | wasm/인터프리터 | Node 인터프리터 | Node wasm |
|---|---|---|---|---|---|---|---|---|---|---|
| alu | 24.3 | 30.7 | 216.0 | 8.9 | 23.0 | 28.2 | 227.3 | 9.9 | 26.3 | 204.7 |
| memory | 26.8 | 28.0 | 141.8 | 5.3 | 25.4 | 22.7 | 122.7 | 4.8 | 28.5 | 149.5 |
| call | 25.0 | 38.5 | 118.3 | 4.7 | 24.9 | 30.6 | 95.7 | 3.8 | 28.5 | 124.3 |
| string | 56.1 | 57.0 | 57.6 | 1.0 | 56.3 | 53.8 | 54.5 | 1.0 | 64.1 | 64.7 |
| x87 | 11.0 | 10.3 | 10.2 | 0.9 | 11.6 | 10.7 | 10.7 | 0.9 | 11.8 | 11.6 |
| mixed | 45.0 | 48.2 | 60.5 | 1.3 | 44.0 | 46.7 | 61.0 | 1.4 | 49.5 | 70.7 |

* 세 엔진의 순서와 배율은 Node와 같은 모양이다. 번역 범위 안의 alu, memory, call에서 wasm이 인터프리터의 3.8~9.9배다. 범위 밖의 string과 x87은 1.0배 안팎이다.
* Firefox는 alu에서 가장 빠르고, call에서 가장 느리다. call의 차이(95.7 대 118.3)는 세 번 모두 같은 방향이었다(Firefox 91.3~100.0, Chromium 118.1~121.1). **추정**: `call_indirect`로 부르는 검사 도우미와 블록 함수 호출의 비용이 엔진마다 다르다. 측정으로 확인하지 않았다(미확정).
* 브라우저의 인터프리터는 Node보다 2~13% 느리다. Node 값은 다른 날 보통 wasm32 빌드(`-sNODERAWFS`)로 잰 것이다. **추정**: Worker 안의 실행과 브라우저 엔진 설정의 차이다.
* 모듈 설치는 Chromium에서 모듈당 100 µs다(34개, 94.1~105.9). Firefox의 값(117.6~147.1 µs)은 신뢰하지 않는다. 교차 출처 격리가 없는 페이지에서 Firefox는 `performance.now()`를 1 ms 단위로 내므로, 34개 설치의 총 4~5 ms가 그대로 양자화된다.
* CI 러너의 수치는 변동이 커서 기록하지 않는다. 작업 요약에만 남는다. WebKit의 처리량은 실기기 Safari(5절)에서 잰다.

*The engines' order and ratios match Node's shape: inside the translation coverage (alu, memory, call) wasm runs at 3.8 to 9.9 times the interpreter, outside it (string, x87) at about 1.0. Firefox is the fastest on alu and the slowest on call, the call gap (95.7 against 118.3) pointing the same way in all three runs (Firefox 91.3 to 100.0, Chromium 118.1 to 121.1); inferred: the cost of the check helper called through `call_indirect` and of block function calls differs by engine, not measured (unresolved). The browsers' interpreter is 2 to 13% slower than Node's, whose figures came from the ordinary wasm32 build (`-sNODERAWFS`) on another day; inferred: running in a Worker and the browser engines' settings. Installation costs 100 µs per module in Chromium (34 modules, 94.1 to 105.9); Firefox's figure (117.6 to 147.1 µs) is not trusted, since Firefox gives `performance.now()` at 1 ms granularity on a page without cross-origin isolation and the 4 to 5 ms total over 34 installations is quantized as is. CI runner figures vary too much to record and stay in the job summary only; WebKit's throughput is to be measured on a real Safari (section 5).*

## 5. 실기기 / Real devices

**미확정**: 사용자가 [가이드](../guides/browser-tests.md) 3절의 절차로 돌린 뒤, 기기(모델, OS, 브라우저 버전, 전원)와 보고 줄, 벤치마크 세 번의 중앙값을 여기에 더한다. README 목표 3의 실용 기준(중급 모바일 브라우저)의 첫 근거가 된다.

*Unresolved: once the user runs the guide's section 3 procedure, the device (model, OS, browser version, power), the report lines and the medians of three benchmark runs go here, the first evidence for README goal 3's practical bar (a mid-range mobile browser).*
