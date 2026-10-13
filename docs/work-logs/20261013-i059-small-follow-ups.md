# #59 작업 로그 : 작은 후속 정리 / #59 work log : small follow-ups

이슈: [#59](https://github.com/reexec/rex86/issues/59) | 설계: [20261013-i059](../design/20261013-i059-small-follow-ups.md) | 지시서: [20261013-i059](../work-orders/20261013-i059-small-follow-ups.md)

## 2026-10-13

호스트: Intel Core i5-7200U(Kaby Lake), Windows 10의 WSL2 Ubuntu 24.04. GCC 13.3, emsdk 6.0.8(Node 24.19.0), Playwright Chromium 153(headless). 브랜치는 `work/i059-small-follow-ups`(main `90dc786`에서)다.

*Host: an Intel Core i5-7200U (Kaby Lake), WSL2 Ubuntu 24.04 under Windows 10, GCC 13.3, emsdk 6.0.8 (Node 24.19.0), Playwright Chromium 153 (headless); branch `work/i059-small-follow-ups` from main `90dc786`.*

### 범위 정리

- 후속 후보 가운데 Node 24 action, libFuzzer 작업의 ctest, `GuestMemory` null base는 #37 후속(2026-10-10)에서 이미 끝났다. 마지막 `main` CI의 주석에도 Node 20 경고는 없었다. 대신 `ubuntu-latest`가 Ubuntu 26.04로 옮겨 간다는 경고가 작업마다 붙어 있었다. 이것을 범위에 넣었다.

*Of the follow-up candidates, Node 24 actions, ctest in the libFuzzer job and the `GuestMemory` null base were already done in #37's follow-up (2026-10-10), and the last `main` CI carried no Node 20 warning; instead every job carried the notice that `ubuntu-latest` moves to Ubuntu 26.04, which joined the scope.*

### 시간 감시(설계 결정 1)

- `src/tools/robust/main.cpp`에 `Watchdog`를 넣었다. `std::thread` 하나, 세대 번호, `condition_variable::wait_for`를 쓴다. pthread 없는 Emscripten에서는 아무것도 하지 않는 같은 이름의 클래스가 된다.
- `CMakeLists.txt`: Emscripten이 아니면 `rex86_robust`에 `Threads::Threads`를 연결한다. ctest `rex86_robust_watchdog`(`PASS_REGULAR_EXPRESSION "VIOLATION seed=1: watchdog"`)를 Emscripten 밖에 등록했다.
- 확인:
  - GCC Debug(`-Werror`) ctest 9/9 통과. 감시 시험은 1.01초에 끝났다(상한 1초, 잠듦 3초).
  - GCC `-fsanitize=thread` 빌드(`setarch -R`로 실행): 감시 발동이 `VIOLATION seed=1: watchdog: the case ran longer than 1 s`, 종료 코드 1이다. 평소 40건은 TSan 경고 없이 `result=ok`다.
  - `--case-seconds 0`이면 감시 스레드를 만들지 않는다(2건 `result=ok`).
  - CI linux-x86(i386 Debian, GCC 12.2): ctest 10/10, 감시 시험 1.00초.

*`Watchdog` went into `src/tools/robust/main.cpp`: one `std::thread`, a generation number and `condition_variable::wait_for`, becoming a do-nothing class of the same name under Emscripten without pthreads. `CMakeLists.txt` links `Threads::Threads` into `rex86_robust` outside Emscripten and registers ctest `rex86_robust_watchdog` (`PASS_REGULAR_EXPRESSION "VIOLATION seed=1: watchdog"`) there. Checked: GCC Debug (`-Werror`) ctest 9/9, the watchdog test ending in 1.01 s (limit 1 s, sleep 3 s); a GCC `-fsanitize=thread` build (run under `setarch -R`) firing with `VIOLATION seed=1: watchdog: the case ran longer than 1 s` and exit code 1, and 40 ordinary cases ending `result=ok` with no TSan warning; `--case-seconds 0` starting no thread (2 cases, `result=ok`); CI linux-x86 (i386 Debian, GCC 12.2) ctest 10/10 with the watchdog test in 1.00 s.*

### A/A 대조(결정 2)

- `compare.js`: `contrast()`로 비율, 범위 분리, 색을 한곳에서 낸다. 세 번째 쪽 `A2`, 세 쪽의 순환 순서, `mark=` 판정(`unsteady` 포함), 보고 키를 더했다. `compare.html`: 체크박스, 설명 문단, `.unsteady` 스타일. `drive_pages.mjs`: `--control on|off`와 `check=control`.
- 로컬 확인(WSL2, Chromium 153): 같은 빌드(0.0.28)를 두 이름으로 조립한 사이트에서 세 번 돌렸다. Playwright의 Chromium은 `libnss3`, `libnspr4`가 없어 뜨지 않았다. sudo 없이 `apt-get download`로 받아 풀고 `LD_LIBRARY_PATH`로 썼다.
  - 대조 켬, 2라운드, 2프레임: `check=control rows=14 with_ratio=14 result=ok`.
  - 대조 끔: `rows=14 with_ratio=0 result=ok`.
  - 버전 불일치(0.0.1 대 0.0.28): `expected=fail got=fail`.
- **관찰**(대조 켬, 2라운드 2프레임이라 잡음이 크다): 같은 빌드끼리인데도 B/A가 #52 규칙에 걸린 행이 셋이었다(interpreter memory 1.876, interpreter x87 0.908, wasm mixed 1.151). 대조를 쓰니 표시된 행은 wasm mixed 하나로 줄었다. memory와 x87은 A2/A가 B/A만큼 흔들려 표시되지 않았다. 대조가 규칙에 걸린 두 행(interpreter blocks, wasm string)은 `unsteady`가 되었다. 쪽마다 실행이 2개면 우연히 갈라질 확률이 2/C(4,2) = 33%다. 그래서 이 실행은 규칙의 오경보율을 보여 주는 것이 아니라 경로가 동작한다는 확인이다.

*`compare.js` computes ratio, separation and tone in one place, `contrast()`, and gains the third side `A2`, the rotating order of three sides, the `mark=` judgment (with `unsteady`) and the report keys; `compare.html` gains the checkbox, an explanatory paragraph and the `.unsteady` style; `drive_pages.mjs` gains `--control on|off` and `check=control`. Local checks (WSL2, Chromium 153) ran three times on a site with one build (0.0.28) under two names, Playwright's Chromium needing `libnss3` and `libnspr4`, fetched without sudo by `apt-get download` and used through `LD_LIBRARY_PATH`: control on, 2 rounds, 2 frames gave `check=control rows=14 with_ratio=14 result=ok`; control off `rows=14 with_ratio=0 result=ok`; a mismatched version (0.0.1 against 0.0.28) `expected=fail got=fail`. Observed, noisily at 2 rounds and 2 frames: three rows met #52's rule with no change (interpreter memory 1.876, interpreter x87 0.908, wasm mixed 1.151); with the control only wasm mixed stayed marked, memory and x87 having A2/A move as far as B/A, and the two rows whose control met the rule (interpreter blocks, wasm string) became `unsteady`. With two runs a side, chance separation is 2/C(4,2) = 33%, so this run checks the path rather than measuring the rule's false-alarm rate.*

### Ubuntu 26.04(결정 3)

- 세 워크플로의 라벨을 바꿔 push했다. 이미지는 `ubuntu26/20260927.149`, arm64는 `ubuntu26-arm64/20260927.135`, 커널은 7.0.0-1012-azure다. GCC 15.2.0과 Clang 21.1.8에서 `-Werror` 빌드가 바로 통과했다. 캠페인 smoke(라벨을 바꾼 `fuzz-campaign.yml`이라 push에서 돌았다)도 12개 작업이 모두 녹색이었다.
- **i386 컨테이너만 실패**: `apt-get update`가 `Temporary failure resolving 'deb.debian.org'`로 끝났다. 다시 돌려도 같았다.
- 진단(임시 step, 모두 지웠다):
  1. `--network host`: 같은 실패. bridge 문제가 아니다.
  2. 호스트의 resolv.conf는 systemd-resolved stub(127.0.0.53)이다. amd64 `debian:bookworm`은 bridge와 host 모두에서 `getent`가 풀렸다. i386은 둘 다 실패했다.
  3. `--add-host`로 DNS를 피해도 i386의 apt는 `does not have a Release file`로 끝났다. 이름 풀이가 아니라 소켓 자체가 문제였다.
  4. bash `/dev/tcp`로 직접 연결: i386은 기본 seccomp에서 `Function not implemented`, `--security-opt seccomp=unconfined`에서는 이름 풀이와 연결이 모두 된다. amd64는 둘 다 된다.
- 결론: 이 이미지의 Docker(29.4.2) 기본 seccomp 프로필이 i386 프로세스의 소켓 시스템 호출을 ENOSYS로 막는다. CI의 i386 컨테이너를 `seccomp=unconfined`로 띄우게 고쳤다. 그 뒤 linux-x86은 ctest 10/10이다. re2DJ는 `modify_ldt` 때문에 이미 같은 옵션을 쓰고 있어 영향이 없다.

*The three workflows' labels changed and were pushed: image `ubuntu26/20260927.149`, arm64 `ubuntu26-arm64/20260927.135`, kernel 7.0.0-1012-azure; GCC 15.2.0 and Clang 21.1.8 passed the `-Werror` builds as they were, and the campaign smoke (run on push, `fuzz-campaign.yml` having changed) had all 12 jobs green. Only the i386 container failed, `apt-get update` ending in `Temporary failure resolving 'deb.debian.org'`, again on a rerun. Diagnosis (temporary steps, all removed): 1 `--network host` failed the same, so not the bridge; 2 the host's resolv.conf is the systemd-resolved stub (127.0.0.53), and amd64 `debian:bookworm` resolved with `getent` on both networks while i386 failed on both; 3 bypassing DNS with `--add-host`, i386's apt still ended in `does not have a Release file`, so sockets themselves failed, not name resolution; 4 a direct bash `/dev/tcp` connect gave `Function not implemented` for i386 under the default seccomp, while `--security-opt seccomp=unconfined` resolved and connected, amd64 doing both either way. Conclusion: this image's Docker (29.4.2) default seccomp profile fails an i386 process's socket calls with ENOSYS; CI's i386 container now runs with `seccomp=unconfined`, after which linux-x86 passed ctest 10/10. re2DJ already uses the same option for `modify_ldt` and is unaffected.*

### README와 #53(결정 4)

- README `GIT_TAG v0.0.2` → `v0.0.28`, 캠페인이 녹색인 tag를 쓰라는 문장.
- #53 작업 로그에 2026-10-13 절(v0.0.27, v0.0.28 캠페인)을 더했다. v0.0.28의 int-i386 warmup-0이 AMD EPYC 9V45(Zen 5)에서 2,000만 건, 불일치 0, 벤더 편차 샤드당 850~907건이다. 넓힌 CMPS 판정이 Zen 5에서 처음 확인되었다.

*README `GIT_TAG v0.0.2` became `v0.0.28`, with a sentence to use a tag whose campaign is green. #53's work log gained a 2026-10-13 section (v0.0.27's and v0.0.28's campaigns): v0.0.28's int-i386 warmup-0 ran 20 million cases on an AMD EPYC 9V45 (Zen 5) with zero mismatches and 850 to 907 vendor deviations a shard, the widened CMPS judgment's first confirmation on Zen 5.*

### 다음 / Next

1. (끝남) CI: 모든 작업이 Ubuntu 26.04에서 녹색이다([실행 38329912537](https://github.com/reexec/rex86/actions/runs/38329912537)). 최종 코드로 브랜치에서 돌린 캠페인 smoke도 녹색이다([실행 38329939309](https://github.com/reexec/rex86/actions/runs/38329939309)).
2. 머지는 사용자 요청을 기다린다. #60이 같은 브랜치에 쌓였다.

*1 (done) CI: every job green on Ubuntu 26.04 (run 38329912537), and the campaign smoke run on the branch with the final code green too (run 38329939309). 2 The merge waits for the user's request; #60 is stacked on the same branch.*
