# #67 설계 : 자기 고리 블록을 생성 코드 안에서 반복 / #67 design : self-looping blocks run in place

이슈: [#67](https://github.com/reexec/rex86/issues/67) | 지시서: [20261014-i067](../work-orders/20261014-i067-block-self-loops.md) | 로그: [20261014-i067](../work-logs/20261014-i067-block-self-loops.md) | 근거: [#65 설계](20261014-i065-translation-dispatch.md)

## 문제와 측정 / Problem and measurement

#65 뒤에도 블록 사이 디스패치(`Cpu::RunUntilStop`의 안쪽 고리와 `WasmBackend::Run`)가 call과 memory 시간의 약 30%다. 블록이 끝난 뒤 어디로 가는지 세었다. 실험용 사본의 안쪽 고리에 임시 계수기를 넣고, 네이티브 평가기로 5프레임을 돌렸다.

| 워크로드 | 번역 블록 실행 | 자기 시작으로 돌아감 |
|---|---:|---:|
| alu | 555,646 | 99.9% |
| memory | 630,408 | 99.9% |
| call | 789,449 | 0%(CALL과 RET으로 다른 블록) |
| mixed | 158,983 | 55% |
| blocks | 1,713,418 | 0%(다른 블록으로 정적 점프) |

한 블록짜리 고리(블록 끝의 분기가 자기 시작으로 감)는 디스패치가 할 일이 없다. 같은 블록을 다시 부를 뿐이다. 게임의 안쪽 고리(복사, 채우기, 픽셀 처리)도 대부분 이 모양이다. 다른 블록을 부르지 않으니 연결 중 가장 단순하고 안전하다.

*After #65, dispatch between blocks (`Cpu::RunUntilStop`'s inner loop and `WasmBackend::Run`) is still some 30% of call's and memory's time. Where blocks go next was counted with a temporary counter in the inner loop of a scratch copy, five frames on the native evaluator: 99.9% of alu's and memory's 555,646 and 630,408 block runs go back to the block's own start, none of call's (CALL and RET lead to other blocks) or blocks' (static jumps to other blocks), and 55% of mixed's. A one-block loop, whose closing branch goes to its own start, gives dispatch nothing to do but call the same block again; games' inner loops (copying, filling, pixel work) mostly have this shape, and since no other block is called it is the simplest and safest kind of linking.*

## 결정 1: 생성 코드의 `loop` / Decision 1: a `loop` in the generated code

자기 시작으로 가는 탈출(정적 목적지가 블록 시작인 `kExit`나 `kExitIf`, 종류 `kContinue`)이 있는 블록은 본체 전체를 wasm `loop`로 감싼다. 그 탈출에서:

1. 이번 반복의 단계를 `done`(지역 변수)에 더한다.
2. `done + instruction_count ≤ max_steps`이고 주의 신호가 꺼져 있으면, 반복 수(`loops`)를 하나 올리고 `loop` 처음으로 간다.
3. 아니면 EIP를 블록 시작으로 쓰고, `done`을 단계 수로 해서 탈출한다.

다른 모든 탈출(조건 탈출, 동적 탈출, 검사 실패, 끝의 대비 탈출)은 `done + 이번 단계`를 보고한다. 본체는 반복마다 상태(`CpuState`)에서 값을 다시 읽으므로, 함수를 다시 부른 것과 같다.

*A block with an exit to its own start (a `kExit` or `kExitIf` of kind `kContinue` whose static target is the block's start) has its whole body wrapped in a wasm `loop`. At that exit it adds the pass's steps to a local `done`; when `done + instruction_count ≤ max_steps` and attention is clear it counts one more pass (`loops`) and branches back to the top of the `loop`, and otherwise it writes the block's start into EIP and exits with `done` steps. Every other exit (conditional, dynamic, a failed check, the closing fallback) reports `done` plus its own steps. The body reads its values from the state (`CpuState`) on every pass, so a pass is the same as calling the function again.*

## 결정 2: 디스패치와 같은 결과 / Decision 2: the same results as dispatch

- **예산**: 디스패치는 남은 예산이 블록 전체를 덮을 때만 블록을 부른다. 제자리 반복도 같은 조건(`done + instruction_count ≤ max_steps`)으로만 다음 반복을 한다. `Run(budget)`이 멈추는 단계는 반복마다 디스패치한 것과 같다. `max_steps`는 `min(남은 예산, 2^30)`이다. 그래서 단계 수가 탈출 코드(31비트)에 들어간다.
- **주의 신호**: 정지 요청과 대기 인터럽트는 `attention`(`std::atomic<bool>`)을 세운다. 반복마다 그 바이트를 읽는다. 다른 스레드의 `RequestStop`도 디스패치와 같은 입도(블록 하나)로 멈춘다.
- **게이트, 모드, 무효화**: 같은 실행 안에서 게이트와 세그먼트 모드는 번역 블록이 바꾸지 않는다. 블록이 자기 코드 페이지(`kTranslated`)에 쓰면 검사(인라인과 도우미 모두)가 실패해 인터프리터로 나간다. 그래서 반복 사이에 블록이 무효가 되는 일은 없다.
- **문맥**: 생성 코드가 읽는 구조체를 `CheckContext`에서 `RunContext`로 넓혔다. 더한 것은 `max_steps`, `attention`(주소), `loops`(반복 수)다. `Backend::Run`은 `RunLimits`(최대 단계, 주의 신호)를 받는다. 평가기 백엔드는 한 번만 돈다(검증용).
- **통계**: `ExitResult::loops`로 제자리 반복을 돌려준다. `TranslationStats::block_runs`는 지금처럼 번역 블록이 돈 횟수를 센다(반복 포함).

*Budget: dispatch calls a block only when the remaining budget covers it whole, and a pass in place runs only on the same condition (`done + instruction_count ≤ max_steps`), so `Run(budget)` stops at the step dispatching every pass would; `max_steps` is `min(remaining budget, 2^30)`, so the step count fits the exit code's 31 bits. Attention: stop requests and pending interrupts raise `attention` (a `std::atomic<bool>`), whose byte every pass reads, so a `RequestStop` from another thread stops at the granularity dispatch has (one block). Gates, modes and invalidation: within one run a translated block changes neither the gates nor the segment modes, and a block writing its own code page (`kTranslated`) fails its check (inline or helper) and leaves for the interpreter, so a block never becomes invalid between passes. The structure generated code reads grows from `CheckContext` into `RunContext`, adding `max_steps`, `attention` (an address) and `loops` (passes run in place); `Backend::Run` takes `RunLimits` (most steps, attention), the evaluator backend running one pass (it is for verification). `ExitResult::loops` returns the passes run in place, and `TranslationStats::block_runs` counts translated block runs as before, passes included.*

## 결정 3: 다른 블록으로의 연결은 측정 뒤 / Decision 3: linking to other blocks after measuring

call(CALL과 RET)과 blocks(정적 점프)는 다른 블록으로 간다. 이것을 연결하는 데는 세 가지가 걸린다.

- **함수 호출**: 목적지 블록은 다른 모듈의 함수다. 그래서 `call_indirect`가 필요하다.
- **스택 깊이**: 사슬이 길어지면 스택이 자란다. tail call(`return_call_indirect`)을 쓰면 피할 수 있지만, 브라우저마다 지원이 다르다.
- **통계와 단계 수**: 사슬 전체의 단계 수와 통계를 문맥에 모아야 한다.

이 작업의 효과를 잰 뒤 범위를 정한다.

*call (CALL and RET) and blocks (static jumps) go to other blocks, and linking them involves calls into functions of other modules (`call_indirect`), stack depth growing along a chain unless tail calls (`return_call_indirect`) are used, whose support varies by browser, and gathering a chain's steps and statistics in the context; its scope is settled after measuring this work.*

## 결과 / Results

호스트 F(Intel Core i5-7200U, WSL2, emsdk 6.0.8, Node 24.19.0). 부하 평균은 4~5였고, v0.0.32와 번갈아 일곱 번 돌린 가장 좋은 값이다(wasm, 10프레임).

| 워크로드 | v0.0.32 | #67 | 변화 |
|---|---:|---:|---:|
| alu | 59.9 | 97.8 | +63% |
| memory | 44.2 | 60.8 | +38% |
| call | 47.7 | 44.6 | —(자기 고리 없음, 변동 안) |
| blocks | 2.26 | 2.82 | —(자기 고리 없음, 중앙값은 2.09와 2.06) |
| mixed | 17.6 | 17.4 | — |

- **확인됨**: 자기 고리가 전환의 99.9%인 alu와 memory가 크게 빨라졌다. 자기 고리가 없는 call, blocks는 변동 안이다. mixed는 자기 고리가 55%지만, 시간 대부분이 인터프리터(x87, REP 문자열)에 있어 차이가 보이지 않는다.
- 다음은 결정 3, 다른 블록으로의 연결(call의 CALL과 RET, blocks의 정적 점프)이다.

*Host F (Intel Core i5-7200U, WSL2, emsdk 6.0.8, Node 24.19.0), load average 4 to 5, the best of seven runs interleaved with v0.0.32 (wasm, 10 frames), as in the table: alu 59.9 to 97.8 (+63%), memory 44.2 to 60.8 (+38%), and call (47.7 and 44.6), blocks (2.26 and 2.82, medians 2.09 and 2.06) and mixed (17.6 and 17.4) within the variation. Confirmed: alu and memory, whose transitions are 99.9% self-loops, got much faster, while call and blocks, with none, stay within the variation, and mixed, 55% self-loops, shows no difference since most of its time is in the interpreter (x87, REP strings). Next is decision 3, linking to other blocks (call's CALL and RET, blocks' static jumps).*

## 검증 / Verification

* `WasmLoopTests`: 고리 프로그램을 예산 1, 3, 7, 50, 100만 단계로 잘라 돌린다. 매번 인터프리터와 멈춘 이유, 단계, 상태, 메모리를 비교한다. 대기 인터럽트(IF 꺼짐)로 주의 신호가 켜진 채로도 비교한다.
* 단위 테스트와 ctest(네이티브, wasm32, 번역 강제 `WASM`), CI. 번역 강제 빌드의 견고성 I8과 IR 차등의 wasm 비교가 무작위 블록을 인터프리터와 맞춘다.
* 측정: v0.0.32와 번갈아 일곱 번, 가장 좋은 값(wasm).

*`WasmLoopTests` runs a loop program with the budget cut into slices of 1, 3, 7, 50 and a million steps, comparing stop reason, steps, state and memory with the interpreter every time, and again with attention raised by a pending interrupt while IF is clear. Unit tests and ctest (native, wasm32, forced `WASM`) and CI, the robustness I8 in forced builds and the IR differential's wasm comparisons matching random blocks against the interpreter. Measurement: the best of seven runs interleaved with v0.0.32 (wasm).*

## 소비자 영향 / Consumer impact

없다. 공개 계약과 결과는 그대로다. 번역이 켜진 소비자에서 한 블록짜리 고리가 디스패치 없이 돈다.

*None: the public contract and results stay; with translation on, consumers' one-block loops run without dispatch.*
