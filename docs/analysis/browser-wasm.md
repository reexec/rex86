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
* **소비자 영향(추정)**: 설치가 거절되면 코어는 그 블록을 인터프리터로 돌리므로 결과는 맞다(#45 계약). 하지만 블록 하나에 모듈 하나를 쓰는 지금의 백엔드는 Firefox에서 프로세스당 1만 6천 개 남짓의 모듈로 묶인다. 번역 실행기의 크기 한도(65,536)보다 훨씬 작다. 무효화 뒤의 재번역도 수집 전까지 이 수에 더해진다. 큰 게스트나 오래 도는 게스트는 번역을 잃을 수 있다. 여러 블록을 모듈 하나로 묶는 설치(#45 결정 2가 남긴 최적화)와 모듈 수 예산이 근거를 얻었다. [#49](https://github.com/reexec/rex86/issues/49)에서 다뤘다(3.1절).

*Inferred: a dropped module's executable code memory returns only at a garbage collection, which running short of code memory does not start, so creation fails once the process's executable budget is full unless JS heap allocation triggers a collection; 16,349 looks like a per-process executable budget divided by a fixed per-module granule, the actual figures being unresolved (to be checked in SpiderMonkey's `ProcessExecutableMemory`). Confirmed for rex86's tools: `rex86_irdiff --cases 200000` (about 250,000 generated modules) had 9 installations refused in Firefox, matching its `mismatches=` total of 9, irdiff counting refusals as mismatches, with no wrong translation; the tool's copies of module bytes into JS make garbage that usually triggers collections, hence the rarity; a finished Worker's modules are not reclaimed at once, so a tool started right after the unit tests (thousands of generated modules) failed from instantiating its main `.wasm`, and passed after a wait of 3 seconds or more. The suite waits 5 seconds between tests, runs 5,000 IR differential cases in browsers (Node's ctest keeps 20,000 and the release campaign more), and restarts a run that failed for want of memory alone up to twice after 15 seconds, counting the retries in the report; with no wait, those retries did not recover either, the failed runs leaving more modules behind. Consumer impact (inferred): a refused installation leaves the block to the interpreter, so results stay correct (#45's contract), but the current one-module-per-block backend is bounded in Firefox by some 16,000 modules per process, far below the runtime's size limit (65,536), and re-translations after invalidation add to the count until a collection; a large or long-running guest can lose translation. Installing several blocks per module (an optimization #45 decision 2 left open) and a module count budget gain their grounds, taken up in #49 (section 3.1).*

### 3.1 묶음 설치 뒤(#49) / After batched installation (#49)

근거: [#49 설계](../design/20261012-i049-wasm-module-batching.md), [로그](../work-logs/20261012-i049-wasm-module-batching.md). 호스트 E(AMD Ryzen 5 5600X, Windows 11 Pro x64), emsdk 3.1.74, Node 24.19.0, Playwright 1.63.0(Chromium 153.0.8010.12, Firefox 155.0), 2026-10-12~13.

*Grounds: the #49 design and log. Host E (AMD Ryzen 5 5600X, Windows 11 Pro x64), emsdk 3.1.74, Node 24.19.0, Playwright 1.63.0 (Chromium 153.0.8010.12, Firefox 155.0), 2026-10-12 to 13.*

* **확인됨**(Firefox, Windows): 한도는 모듈 수에 걸린다. 함수를 1, 16, 64개 내보내는 모듈 모두 16,349개째에 실패했고, 설치된 함수는 16,349, 261,584, 1,046,336개였다. wasm 32 KB 모듈까지는 한도가 그대로였고, 51 KB 모듈에서는 8,220개로 절반이 되었다. Linux(#48)와 같은 수다.
* **추정**: 1 GiB의 실행 메모리를 64 KiB 단위로 나눈 16,384에서 엔진 몫을 뺀 값이다. SpiderMonkey 소스로는 확인하지 않았다.
* **확인됨**(Chromium): 모듈 한도 탐침(`drive.mjs --module-limit`)이 상한 4만 개까지 실패 없이 만들었다(2.7초).
* **확인됨**(WebKit 26.6, Playwright의 Linux 빌드, CI `browsers` [실행 38276824251](https://github.com/reexec/rex86/actions/runs/38276824251)): 탐침이 상한 4만 개까지 실패 없이 만들었다(2.4초). 같은 실행에서 Chromium 4만 개, Firefox 16,349개였다.
* **미확정**: 실기기 Safari(macOS, iOS)의 한도. 가이드 3절 6단계로 잰다.

*Confirmed (Firefox, Windows): the limit counts modules; modules exporting 1, 16 and 64 functions all failed at the 16,349th, with 16,349, 261,584 and 1,046,336 functions installed; the limit held up to 32 KB wasm modules and halved to 8,220 at 51 KB; the same count as on Linux (#48). Inferred: 16,384, 1 GiB of executable memory in 64 KiB units, less the engine's share; not checked in SpiderMonkey's source. Confirmed (Chromium): the module limit probe (`drive.mjs --module-limit`) made its cap of 40,000 without a failure (2.7 seconds). Confirmed (WebKit 26.6, Playwright's Linux build, CI `browsers` run 38276824251): the probe made its cap of 40,000 without a failure (2.4 seconds), Chromium making 40,000 and Firefox 16,349 in the same run. Unresolved: real-device Safari's (macOS, iOS) limit, measured through guide section 3 step 6.*

**`blocks` 워크로드**(서로 다른 두 명령 블록 24,576개, 바퀴마다 모두 진입), `rex86_bench --engine wasm --only blocks --frames 60`, 새 브라우저 하나에 한 번씩. 묶음 없음과 예산 없음은 `--blocks-per-module 1 --max-modules 0`이다.

*The `blocks` workload (24,576 distinct two-instruction blocks, all entered every lap), `rex86_bench --engine wasm --only blocks --frames 60`, once per fresh browser; no batching and no budget is `--blocks-per-module 1 --max-modules 0`.*

| 엔진 / engine | 설정 / setting | MIPS | 설치 / installed | 거절 / refused | 설치 시간 / install time |
|---|---|---:|---:|---:|---:|
| Firefox | 묶음 없음, 예산 없음 / none | 0.34 | 16,339 | **8,241** | 157.8 s (9.7 ms/모듈, 한도 근처에서 느려짐 / slowing near the limit) |
| Firefox | 기본값(묶음 16) / defaults (16) | 7.3 | 1,537 | **0** | 1.1 s |
| Node | 묶음 없음, 예산 없음 / none | 2.8 | 24,580 | 0 | 2.7 s |
| Node | 기본값 / defaults | 9.5 | 1,537 | 0 | 0.23 s |

* **확인됨**: 묶음 16으로 Firefox의 설치 거절이 0이 되었고, 블록 24,580개가 모두 번역되었다. 묶음 없이 Firefox는 16,339개를 설치한 뒤 나머지를 거절했다. 거절된 블록은 인터프리터로 돌았고 결과 검증(`verified=ok`)은 통과했다.
* **확인됨**: 이 워크로드에서 번역은 인터프리터(Node 3.0, Chromium 3.5, Firefox 3.8 MIPS)보다 묶음 16에서만 빠르다. 두 명령 블록은 블록마다 드는 디스패치 비용이 커서, 모듈이 많으면(묶음 없음) 인터프리터보다 느리다. 모듈 수와 처리량의 관계(인스턴스가 많을 때의 호출 비용인지, 엔진의 계층 컴파일 차이인지)는 미확정이다.

*Confirmed: with 16-block batches Firefox refused nothing and all 24,580 blocks were translated, while without batching it installed 16,339 and refused the rest, those blocks running on the interpreter and the result check (`verified=ok`) passing. Confirmed: on this workload translation beats the interpreter (Node 3.0, Chromium 3.5, Firefox 3.8 MIPS) only with 16-block batches; two-instruction blocks pay a large dispatch cost per block, and with many modules (no batching) translation is slower than the interpreter; what links the module count to throughput (call cost across many instances, or the engines' tiered compilation) is unresolved.*

**묶음 크기와 대기**(세 번 번갈아 돌린 중앙값, MIPS, 괄호는 첫 프레임 ms). `w`는 `batch_wait`, 표시가 없으면 4,096. 묶음 1과 4의 `blocks`는 작업 집합(24,576블록)이 예산(4,096모듈)을 넘어 축출이 일어나므로 축출 억제를 넣은 뒤 다시 쟀다.

*Batch size and wait (medians of three interleaved runs, MIPS, first-frame ms in parentheses), `w` being `batch_wait`, 4,096 where unmarked; `blocks` at batch sizes 1 and 4 has a working set (24,576 blocks) beyond the budget (4,096 modules), so evictions occur, and was measured again after the eviction backoff.*

| 엔진 / engine | 설정 / setting | alu | call | mixed | blocks | blocks 모듈 / modules | 설치 µs/모듈 / install µs per module |
|---|---|---:|---:|---:|---:|---:|---:|
| Node | 인터프리터 / interpreter | 19.5 (87) | 17.3 (57) | 30.8 (39) | 3.0 | | |
| Node | 1 | 184.5 (15) | 106.4 (13) | 52.2 (29) | 1.7 | 114,708 | 110 |
| Node | 4 | 187.1 (23) | 109.1 (15) | 62.6 (24) | 3.4 | 22,533 | |
| Node | **16** | 202.7 (19) | 106.3 (13) | 50.8 (23) | 11.4 | 1,537 | 148 |
| Node | 64 | 195.6 (22) | 110.0 (14) | 62.1 (27) | 12.6 | 385 | 251 |
| Node | 16, w256 | 180.9 (19) | 91.9 (18) | 58.6 (26) | 10.8 | 1,537 | |
| Node | 16, w65536 | 188.0 (48) | 106.0 (31) | 60.7 (31) | 12.9 | 1,537 | |
| Chromium | 인터프리터 / interpreter | 23.2 (67) | 21.3 (42) | 41.0 (26) | 3.5 | | |
| Chromium | 1 | 185.0 (16) | 109.3 (11) | 61.6 (22) | 1.2 | 114,708 | 239 |
| Chromium | 4 | 159.7 (17) | 100.5 (17) | 59.8 (24) | 3.5 | 22,533 | |
| Chromium | **16** | 184.2 (21) | 98.1 (18) | 59.2 (23) | 12.7 | 1,537 | 145 |
| Chromium | 64 | 194.1 (19) | 93.8 (20) | 58.3 (30) | 16.3 | 385 | 233 |
| Chromium | 16, w256 | 166.9 (17) | 94.7 (12) | 56.1 (24) | 12.8 | 1,537 | |
| Chromium | 16, w65536 | 143.1 (61) | 94.3 (41) | 59.1 (26) | 13.6 | 1,537 | |
| Firefox | 인터프리터 / interpreter | 21.7 (72) | 23.5 (47) | 42.8 (28) | 3.8 | | |
| Firefox | 1 | 147.8 (8) | 74.2 (20) | 54.9 (22) | 2.0 | 114,696 (거절 8 / 8 refused) | 117 |
| Firefox | 4 | 201.3 (11) | 86.1 (17) | 61.2 (25) | 3.8 | 22,533 | |
| Firefox | **16** | 173.9 (11) | 87.0 (19) | 60.8 (26) | 8.8 | 1,537 | 647 |
| Firefox | 64 | 169.0 (10) | 89.3 (15) | 60.7 (25) | 10.4 | 385 | 877 |
| Firefox | 16, w256 | 180.2 (8) | 87.2 (12) | 60.0 (21) | 8.8 | 1,537 | |
| Firefox | 16, w65536 | 157.1 (49) | 90.0 (34) | 59.0 (32) | 8.4 | 1,537 | |

* **확인됨**: 블록이 몇 안 되는 alu, call, mixed(모듈 3~22개)는 묶음 크기 1~64 사이의 차이가 같은 설정의 실행 간 편차 안이다. 이 기계(Windows, 다른 작업과 함께)의 편차는 ±10%를 넘을 때가 있다.
* **확인됨**: `batch_wait` 65,536은 작은 워크로드의 첫 번역을 늦춰 첫 프레임이 길어졌다(Chromium alu 21 → 61 ms, Node 19 → 48 ms). 256은 4,096보다 나은 점이 없었다.
* **확인됨**: 묶음 하나의 설치 시간은 블록 수보다 천천히 는다. Firefox는 모듈당 117 µs(1블록), 647 µs(16블록), 877 µs(64블록)이다. Firefox의 값은 `performance.now()`가 1 ms 단위라 모듈 수천 개의 합으로만 뜻이 있다.
* **확인됨**(축출 억제 전, 같은 빌드에서 억제만 없음): 묶음 1, 예산 4,096의 `blocks`는 Chromium 0.3 MIPS(모듈 76만 2천 개), Firefox 0.5 MIPS(76만 1천 개, 거절 52)였다. 억제 뒤 1.2와 2.0 MIPS, 11만 5천 개가 되었다. Firefox가 76만 개를 거절 52개로 설치한 것은, 설치 도구가 바이트를 JS로 복사하며 만드는 쓰레기가 수집을 일으켜 버린 모듈을 돌려받기 때문으로 보인다(추정, #48의 추정과 같다).
* 기본값: 묶음 16, `batch_wait` 4,096, 예산 4,096 모듈(임시). 고른 이유는 설계 결정 4, 5.

*Confirmed: alu, call and mixed, with few blocks (3 to 22 modules), differ between batch sizes 1 and 64 within the run-to-run spread of one setting, which on this machine (Windows, with other work running) sometimes exceeds ±10%. Confirmed: a `batch_wait` of 65,536 delayed small workloads' first translation and lengthened the first frame (Chromium alu 21 to 61 ms, Node 19 to 48 ms), while 256 was no better than 4,096. Confirmed: a batch's installation time grows more slowly than its block count, Firefox taking 117 µs per module at 1 block, 647 µs at 16 and 877 µs at 64, Firefox's figures meaningful only as sums over thousands of modules given `performance.now()`'s 1 ms granularity. Confirmed (before the eviction backoff, the same build without it): `blocks` with 1-block batches and a budget of 4,096 ran at 0.3 MIPS in Chromium (762,000 modules) and 0.5 in Firefox (761,000, 52 refused), and after the backoff at 1.2 and 2.0 MIPS with 115,000; Firefox installing 761,000 modules with 52 refusals is attributed to the garbage the installer makes copying bytes into JS triggering collections that return dropped modules (inferred, as in #48). Defaults: 16 blocks per module, a `batch_wait` of 4,096 and a budget of 4,096 modules (provisional), chosen per design decisions 4 and 5.*

**모음과 도구**: 브라우저 모음의 IR 차등을 2만 건(Node ctest와 같음)으로 올렸다. wasm 비교는 블록 16개씩 모듈 하나로 설치된다. Chromium과 Firefox가 10/10, 재시도 0으로 통과했다. Firefox는 테스트 사이 대기 0으로도 세 번 모두 10/10, 재시도 0이었다(#48에서는 대기 0이 실패했다). 모음의 기본 대기를 5초에서 1초로 줄였다. Node에서 `rex86_irdiff --cases 500000 --seed 2490000000`은 형태 50만 건(wasm 실행 489,139건, 모듈 30,572개)과 블록 125,001건(모듈 7,813개)에서 불일치 0이었다.

*The suite and tools: the browser suite's IR differential rose to 20,000 cases (as Node's ctest), its wasm comparisons installing 16 blocks per module; Chromium and Firefox passed 10/10 with no retry, Firefox also 10/10 with no retry three times out of three with no wait between tests (where #48's zero wait failed), so the suite's default wait went from 5 seconds to 1. On Node, `rex86_irdiff --cases 500000 --seed 2490000000` found no mismatch over 500,000 forms (489,139 wasm runs, 30,572 modules) and 125,001 blocks (7,813 modules).*

### 3.2 모듈 수와 처리량(#60) / Module count against throughput (#60)

근거: [#60 설계](../design/20261013-i060-retranslation-backoff.md) 결정 4, [로그](../work-logs/20261013-i060-retranslation-backoff.md). 호스트 F(Intel Core i5-7200U, Windows 10의 WSL2 Ubuntu 24.04), emsdk 6.0.8, Node 24.19.0, Playwright의 headless Chromium 153과 Firefox(Linux 빌드). 2026-10-13. 이 기계는 실행 간 변동이 커서 값은 범위로 적는다.

*Grounds: the #60 design, decision 4, and log. Host F (Intel Core i5-7200U, WSL2 Ubuntu 24.04 under Windows 10), emsdk 6.0.8, Node 24.19.0, Playwright's headless Chromium 153 and Firefox (Linux builds), 2026-10-13; this machine varies much run to run, so figures are ranges.*

**`blocks`, 묶음 16과 64를 번갈아 세 번씩**(MIPS, 기본 20프레임):

| 엔진 / engine | 16 | 64 |
|---|---:|---:|
| Chromium | 3.32–4.45 | 4.22–5.30 |
| Firefox | 2.53–2.89 | 3.67–3.89 |
| Node | 3.96–4.43 | 5.17–5.25 |

**설치와 그 뒤를 나눔**(Node, 40프레임, 두 번씩):

| 묶음 / batch | 모듈 / modules | 설치 / install | 프레임 p50 / frame p50 | 첫 프레임 / first frame |
|---|---:|---:|---:|---:|
| 16 | 1,537 | 512–757 ms | 115–118 ms | 528–687 ms |
| 64 | 385 | 305–338 ms | 97–101 ms | 688–853 ms |

* **확인됨**: 3.1절(호스트 E)과 같은 방향이다. 블록이 많은 워크로드에서는 묶음 64가 16보다 빠르다.
* **확인됨**: 차이의 일부만 설치 비용이다. 모든 블록이 설치된 뒤의 프레임(p50)도 64가 약 14% 빠르다. 살아 있는 모듈이 많을수록 블록 호출이 느려진다는 뜻이다. 엔진 내부의 원인(인스턴스 전환 등)은 확인하지 않았다(**추정**).
* **확인됨**: 대가로 첫 프레임이 늦어진다(모듈 하나의 설치가 길어짐, 모듈당 333~493 µs에서 793~877 µs).
* **확인됨**(단발): 작업 집합이 모듈 예산(4,096)을 넘는 묶음 1과 4에서는 축출이 계속 돈다. `blocks`가 Chromium 0.50과 1.39, Firefox 0.49와 1.00 MIPS로 인터프리터(1.92, 1.38)보다 느렸다. #49의 축출 억제가 상한(64배)에 닿아도 그렇다. 기본값(묶음 16)으로는 블록 6만 5천 개까지 축출이 없다.
* 기본값은 바꾸지 않았다. 묶음을 키우는 것은 첫 프레임과 맞바꾸는 선택이라, 실제 소비자 게스트의 측정과 함께 정한다.

*Confirmed: the direction of section 3.1 (host E): with many blocks, 64-block batches beat 16. Only part of it is installation: once every block is installed, frames (p50) still run some 14% faster at 64, so more live modules make block calls slower; the engine-side cause (switching instances, say) was not examined (inferred). The price is a later first frame, one module's installation taking longer (333 to 493 µs a module against 793 to 877 µs). Single runs: at batch sizes 1 and 4 the working set exceeds the module budget (4,096) and eviction keeps going, `blocks` running at 0.50 and 1.39 MIPS in Chromium and 0.49 and 1.00 in Firefox, below the interpreter (1.92, 1.38), even with #49's backoff at its 64-fold cap; at the default (16) nothing is evicted up to 65,000 blocks. The defaults stay: a larger batch trades the first frame away, to be decided with measurements of a real consumer guest.*

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

**미확정**: 사용자가 [가이드](../guides/browser-tests.md) 3절의 절차로 돌린 뒤, 기기(모델, OS, 브라우저 버전, 전원)와 보고 줄, 벤치마크 세 번의 중앙값을 여기에 더한다. README 목표 3의 실용 기준(중급 모바일 브라우저)의 첫 근거가 된다. #52부터 릴리스별 페이지가 GitHub Pages(<https://reexec.github.io/rex86/>)에 있어 빌드 없이 열 수 있다. 같은 기기에서 두 버전을 비교한 `[rex86-compare]` 줄도 여기에 더한다([가이드](../guides/browser-tests.md) 4절).

*Unresolved: once the user runs the guide's section 3 procedure, the device (model, OS, browser version, power), the report lines and the medians of three benchmark runs go here, the first evidence for README goal 3's practical bar (a mid-range mobile browser). From #52 each release's page is on GitHub Pages (https://reexec.github.io/rex86/), opened without a build, and `[rex86-compare]` lines comparing two versions on one device go here too (guide section 4).*
