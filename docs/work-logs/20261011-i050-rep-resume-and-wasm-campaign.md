# #50 작업 로그 : 진행 중인 REP 문자열의 재개와 wasm 캠페인 작업 / #50 work log : resuming a REP string in progress, and the wasm campaign job

이슈: [#50](https://github.com/reexec/rex86/issues/50) | 설계: [20261011-i050](../design/20261011-i050-rep-resume-and-wasm-campaign.md) | 지시서: [20261011-i050](../work-orders/20261011-i050-rep-resume-and-wasm-campaign.md)

## 2026-10-11

호스트: AMD Ryzen 5 5600X(Zen 3) 12 스레드, Windows 11 Pro, Visual Studio 2022(MSVC, x64 Release), emsdk 3.1.74(Node 24.19.0), 비교용 Node 20.18.0(nodejs.org 배포판). 브랜치는 `work/i052-pages-ab-record`(main `df15273`에서. #52의 배포 뒤 기록 커밋 둘 위에서 이어 작업, AGENTS.md 브랜치 규칙).

### I8 위반

- **재현**: `rex86_robust --case 2200826386`이 Windows x64 MSVC Release에서도 `I8: translation and the interpreter ran the same case differently`를 냈다. 이벤트를 나란히 놓으면 run 8(예산 12049)에서 갈라진다. 인터프리터는 `retired=8181 fault=1 address=0000B000`, 번역은 `retired=7921 fault=1 address=617812F1`이고 둘 다 `eip=0000ABEB`이다. 이슈에 적힌 Linux GCC의 값과 같다.
- **추적**(임시 계측, 커밋하지 않음):
  - run 8 직전의 코드는 `ABE8: A9 0D49F265`(test eax, imm), `ABED: 64 AB`(fs: stosd), `ABEF: 71 F7`(jno ABE8)이었다. EAX=`61781290`, EDI=`82AE`, DF=0, 메모리 크기 `0xB000`이다.
  - 블록 단위로 찍으면, 번역 쪽은 매 반복 `T ABE8..ABED kind=1`(저장에서 빠져나옴), `I ABED..ABEF`(stosd 한 단계), `T ABEF..ABE8`을 되풀이한다. EDI가 `ABE6`에 오면 `ABE8`이 `78 61`(JS)로 바뀐다. 이어 `T ABEA..ABEB kind=1`(dec ecx 뒤 빠져나옴), `I ABEB..ABEB n=1`(REP STOSD의 첫 반복, `ABEA..ABED`를 덮어씀)이 나온다. 그다음 `I ABEB..ABEB n=0`이 폴트였다.
  - 인터프리터만 쓴 쪽은 run 8 전체가 한 블록(`n=8181`)이고, EDI가 `AFFE`까지 간 뒤 폴트를 냈다.
- **원인 확정**(번역 없이): `rep_budget_test`의 분할 동등성 목록에 `mov al, 0x90; rep stosb`(EDI가 REP 자신을 가리킴, ECX=2)를 넣으니, 예산 1과 2에서 단계 수와 레지스터가 한 번에 돌린 결과와 달랐다. 결함은 #32의 재개(다시 가져와 디코드)에 있다. 설계 문제 1에 적었다.
- **구현**(설계 결정 1):
  - `StepBudget::partial`과 `BlockLimits::partial`/`resume`을 더했다. `exec_strings`는 `kPartial`로 돌아갈 때 디코드를 복사한다. 재개 중인 명령이 다시 `kPartial`이면 같은 객체라 복사하지 않는다.
  - `RunBlockLoop`: 블록 루프의 단계 뒤 처리를 `ContinueBlock`으로 뺐다. `resume`이 있으면 첫 단계를 루프 앞에서 `ExecuteDecoded`로 실행한다.
  - `Cpu`: `partial_decode_`(첫 블록에서 만듦), `partial_32bit_`. 재개 조건은 `in_progress_`, 같은 선형 EIP, 같은 CS 모드다. `InstructionInProgress` 주석에 규칙을 적었다.
- **테스트**:
  - `rep_budget_test`: 자기 바이트를 덮어쓰는 REP 셋(STOSB, DF=1 STOSD, MOVSB)의 분할 동등성(예산 1, 2, 7, 예열 있음과 없음). 같은 프로그램의 번역(평가기, 문턱 0)과 인터프리터 비교. EIP를 옮기거나 CS를 16비트로 바꾼 뒤의 재개는 다시 가져옴.
  - `robust_test`: `seed=2200826386`.
  - **고치기 전 코드**(코어 변경만 stash)에서는 새 테스트가 52건 실패했다. 분할 동등성, 번역 비교(단계 수, 레지스터, EIP, 메모리), seed 회귀(I8) 모두다. 고친 뒤에는 `checks=2193 failures=0`이다.
- **검증**:
  - `rex86_robust --case 2200826386`: `result=ok`.
  - `rex86_robust --cases 200000 --seed 2200000000`(x64 Release): `violations=0`, 11분 57초.
  - Windows x64 Release ctest 6/6, wasm32(Node 24) ctest 7/7.
  - 벤치마크(`--only string,mixed,alu`, 고치기 전 `main`과 번갈아 세 번, 중앙값, MIPS): string 75.7 → 73.5(0.97. 고치기 전 판의 세 값이 68.2~75.8), mixed 51.9 → 53.8(1.04), alu 23.4 → 28.3(1.21, 세 번 모두). alu는 REP가 없으므로, 블록 루프 구조를 바꾼 데서 온 코드 배치 효과로 추정한다(미확정). 후퇴는 없다.

### wasm-translate 중단

- **측정**(같은 Node용 빌드 `web-wasm-release`, `rex86_irdiff.js --cases 500000 --seed 2390000000`, shard 하나):
  - Node 24.19.0: 형태 50만 건(wasm 실행 489,437번)과 블록 125,001건, 불일치 0. 137초, 최대 168 MB. 실행 중에는 120 MB 안팎이었다.
  - Node 20.18.0: 30초마다 711, 1094, 1326, 1499 MB…로 늘었다. 1,500초에 4,418 MB에서 끊었고, 형태 단계의 결과 줄도 나오지 않았다.
- CI의 그 단계는 shard 넷을 동시에 돌린다. 캠페인 로그의 `EMSDK_NODE`는 20.18.0이고, 작업은 Node를 따로 설치하지 않았다. 설계 결정 2에 따라 `wasm-translate`에 `actions/setup-node`(Node 24)를 더했다. `actionlint`는 보고가 없다.
- 실제 효과는 이 판의 tag 캠페인에서 확인한다.

### 문서

- 설계, 지시서, 이 로그. #32 설계의 결정 6에 #50 보완을 더했다. ARCHITECTURE의 `Run` 루프 문단. README 경고 문구(번역 첫 판이 있음, 기본은 꺼짐. #45부터 남아 있던 "번역 백엔드는 아직 없다"를 고침).

*Host: AMD Ryzen 5 5600X (Zen 3), 12 threads, Windows 11 Pro, Visual Studio 2022 (MSVC, x64 Release), emsdk 3.1.74 (Node 24.19.0), Node 20.18.0 (the nodejs.org build) for comparison; branch `work/i052-pages-ab-record` (from main `df15273`, continuing over #52's two after-deployment record commits per the AGENTS.md branch rule).*

*The I8 violation. Reproduction: `rex86_robust --case 2200826386` reported `I8: translation and the interpreter ran the same case differently` on Windows x64 MSVC Release too; side by side the events part at run 8 (budget 12049), the interpreter at `retired=8181 fault=1 address=0000B000` and translation at `retired=7921 fault=1 address=617812F1`, both `eip=0000ABEB`, the issue's Linux GCC figures. Tracing (temporary instrumentation, not committed): before run 8 the code read `ABE8: A9 0D49F265` (test eax, imm), `ABED: 64 AB` (fs: stosd), `ABEF: 71 F7` (jno ABE8) with EAX=`61781290`, EDI=`82AE`, DF=0 and a `0xB000` memory; per block, translation repeated `T ABE8..ABED kind=1` (exit at the store), `I ABED..ABEF` (the stosd step) and `T ABEF..ABE8`; once EDI reached `ABE6`, `ABE8` became `78 61` (JS), then `T ABEA..ABEB kind=1` (exit after dec ecx) and `I ABEB..ABEB n=1` (the REP STOSD's first iteration, overwriting `ABEA..ABED`), and the next `I ABEB..ABEB n=0` faulted, while the interpreter alone ran all of run 8 as one block (`n=8181`), EDI reaching `AFFE` before the fault. Cause confirmed without translation: `mov al, 0x90; rep stosb` (EDI at the REP itself, ECX=2) in `rep_budget_test`'s split-equivalence list differed from the at-once run in steps and registers at budgets 1 and 2, putting the defect in #32's resume (fetch and decode again), as design problem 1 records. Implementation (design decision 1): `StepBudget::partial` and `BlockLimits::partial`/`resume`, `exec_strings` copying the decode on `kPartial` (not when the resumed instruction is that very object); `RunBlockLoop` with the after-step handling moved into `ContinueBlock` and a `resume` running the first step ahead of the loop through `ExecuteDecoded`; `Cpu`'s `partial_decode_` (made at the first block) and `partial_32bit_`, resuming under `in_progress_`, the same linear EIP and the same CS mode, with the rule in `InstructionInProgress`'s comment. Tests: `rep_budget_test`'s split equivalence of three self-overwriting REPs (STOSB, STOSD with DF=1, MOVSB; budgets 1, 2 and 7, warm and cold), the same programs under translation (evaluator, threshold 0) against the interpreter, and a re-fetch after moving EIP or switching CS to 16-bit; `robust_test` pins `seed=2200826386`. On the code before the change (core edits stashed) the new tests failed 52 checks, covering split equivalence, the translation comparison (steps, registers, EIP, memory) and the seed (I8); after it `checks=2193 failures=0`. Verification: `rex86_robust --case 2200826386` `result=ok`; `rex86_robust --cases 200000 --seed 2200000000` (x64 Release) `violations=0` in 11 min 57 s; Windows x64 Release ctest 6/6 and wasm32 (Node 24) ctest 7/7; the benchmark (`--only string,mixed,alu`, three runs interleaved with the pre-change `main`, medians in MIPS) gave string 75.7 to 73.5 (0.97, the pre-change runs themselves spanning 68.2 to 75.8), mixed 51.9 to 53.8 (1.04) and alu 23.4 to 28.3 (1.21 in all three), alu having no REP, so inferred a code layout effect of the restructured block loop (unresolved); no regression.*

*The wasm-translate stall. Measured on the same Node build (`web-wasm-release`, `rex86_irdiff.js --cases 500000 --seed 2390000000`, one shard): Node 24.19.0 ran 500,000 forms (489,437 wasm runs) and 125,001 blocks without a mismatch in 137 seconds at 168 MB peak, about 120 MB while running; Node 20.18.0 grew every 30 seconds through 711, 1094, 1326, 1499 MB and onwards, and was stopped at 1,500 seconds at 4,418 MB without even the forms result line. CI's step runs four shards at once, the campaign log's `EMSDK_NODE` is 20.18.0, and the job installed no Node of its own; per design decision 2 `wasm-translate` gains `actions/setup-node` (Node 24), actionlint reporting nothing; the effect is checked in this version's tag campaign. Documents: the design, work order and this log; a #50 addendum to #32's decision 6; ARCHITECTURE's `Run` loop paragraph; the README warning (translation's first version exists, off by default, fixing "translation backends do not exist yet" left since #45).*
