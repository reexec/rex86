# 트랩 플래그 단일 스텝으로 명령 하나 관찰하기 / Observing one instruction with the trap-flag single step

출처: [Intel SDM Vol. 3](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) 18.3.1.4(단일 스텝 예외 조건), 6.15(예외 목록), Vol. 2 POPF와 REP | Linux: [`signal(7)`](https://man7.org/linux/man-pages/man7/signal.7.html), [`sigaltstack(2)`](https://man7.org/linux/man-pages/man2/sigaltstack.2.html), [`getcontext(3)`](https://man7.org/linux/man-pages/man3/getcontext.3.html) | 이 저장소의 사용처: [#22 설계](../design/20261008-i022-integer-host-fuzz-and-trace.md) 결정 1, `tests/host/linux/int_fuzz.cpp`

사용자 모드 프로세스에서 임의의 x86 명령 **하나**를 실제 CPU로 실행하고 직후의 레지스터와 플래그를 얻는 일반 기법이다. 디버거 없이 한 프로세스 안에서 끝난다.

*A general technique for running **one** arbitrary x86 instruction on the real CPU from a user-mode process and reading the registers and flags right after it, inside one process and without a debugger.*

## 원리 / Principle

* EFLAGS.TF(비트 8)가 **명령 시작 시점에** 켜져 있으면, 그 명령이 끝난 뒤 #DB(벡터 1) 트랩이 온다. POPF/POPFD가 TF를 켜면 POPF 자신이 아니라 **다음 명령** 뒤에 트랩이 온다. 그래서 `popfd; <명령>`이 "그 명령 하나 실행"이 된다.
* 트랩은 명령 완료 뒤, 다음 명령 인출 전에 온다. 분기, CALL, RET이 어디로 가든 목적지는 인출되지 않고 보고된 EIP가 목적지다.
* 명령이 폴트를 내면 트랩 대신 그 폴트가 오고, 보고되는 상태는 명령 시작 전 상태다(폴트의 정확성).
* REP 문자열은 **반복마다** 단일 스텝 트랩을 낸다. 반복 중이면 보고된 EIP가 명령 시작 주소다. 같은 상태가 인터럽트나 예외로 중단된 REP에도 정의되어 있다(SDM Vol. 2B REP 항목): 원본·목적지 레지스터는 다음 원소를, EIP는 문자열 명령을, ECX는 마지막으로 성공한 반복 뒤의 값을 가리키고, 처리기에서 돌아오면 이어서 실행된다. 코어의 예산 단위인 단계가 이것이다(#32).
* TF를 끄는 명령(TF=0인 값을 꺼낸 POPF)은 트랩을 내지 않는다. 명령 뒤에 INT3를 두면 그것이 잡는다.

*With EFLAGS.TF set **when an instruction starts**, a #DB trap (vector 1) follows its completion; a POPF/POPFD that sets TF traps after the **next** instruction, not itself, so `popfd; <instruction>` means "run exactly that instruction". The trap arrives after completion and before the next fetch, so wherever a branch, CALL or RET goes, the target is never fetched and the reported EIP is the target. A faulting instruction delivers its fault instead, with the pre-instruction state (precise faults). REP strings trap after **every iteration**, reporting the instruction's own address while iterations remain; the SDM (Vol. 2B, REP) defines the same state for a REP suspended by an interrupt or exception, the source and destination registers at the next elements, EIP at the string instruction and ECX as the last successful iteration left it, the handler's return resuming it, and that is the core's budget unit, the step (#32). An instruction that clears TF (a POPF popping TF = 0) does not trap; an INT3 placed after the instruction catches it.*

## Linux i386에서의 신호 대응 / The signal mapping on Linux i386

| 예외 | 신호 | `REG_TRAPNO` |
|---|---|---|
| #DE | SIGFPE | 0 |
| #DB(단일 스텝) | SIGTRAP | 1 |
| INT3 | SIGTRAP | 3 |
| INTO(#OF) | SIGSEGV | 4 |
| BOUND(#BR) | SIGSEGV | 5 |
| #UD | SIGILL | 6 |
| #SS | SIGBUS | 12 |
| #GP | SIGSEGV | 13 |
| #PF | SIGSEGV | 14, 주소는 `uc_mcontext.cr2` |
| #AC | SIGBUS | 17 |

`SA_SIGINFO` 처리기의 세 번째 인자 `ucontext_t`의 `uc_mcontext.gregs[]`에 `REG_EAX`~`REG_EDI`, `REG_ESP`, `REG_EIP`, `REG_EFL`이 있다. 폴트 컨텍스트의 EFLAGS에는 RF(비트 16)가 켜져 있을 수 있다.

*The `ucontext_t` handed to an `SA_SIGINFO` handler carries `REG_EAX`..`REG_EDI`, `REG_ESP`, `REG_EIP` and `REG_EFL` in `uc_mcontext.gregs[]`, and the #PF address in `uc_mcontext.cr2`; a fault context's EFLAGS may have RF (bit 16) set.*

## 실무 주의 / Practical notes

* **대체 신호 스택**: 시험 명령은 ESP를 마음대로 쓴다. `sigaltstack`과 `SA_ONSTACK`이 없으면 신호 전달 자체가 실패한다.
* **복귀**: 처리기에서 `siglongjmp`로 돌아온다. `sigsetjmp(env, 1)`로 신호 마스크를 저장해야 처리기 안의 신호가 다시 막히지 않는다. REP 반복 트랩이면 longjmp하지 말고 처리기에서 돌아가 다음 반복을 실행시킨다(명령 시작 주소 = 반복 중).
* **플래그 정리**: 신호 전달은 TF와 DF를 끄지만 시험 명령이 켠 AC와 NT는 남을 수 있다. longjmp 직후 `pushf; and; popf`로 지운다. AC가 켜진 채로 정렬되지 않은 접근을 하면 #AC가 난다.
* **IF와 IOPL**: CPL 3에서 POPF는 IF와 IOPL을 바꾸지 못하고 조용히 무시한다. 비교에서 뺀다.
* **같은 주소**: `MAP_FIXED_NOREPLACE`로 낮은 고정 주소(예: 0x10000)에 영역을 두면 에뮬레이터 쪽도 같은 게스트 주소를 써서 레지스터 값이 그대로 비교된다. `vm.mmap_min_addr`(보통 4096 또는 65536)보다 아래에는 매핑할 수 없다. 그 아래와 영역 주변은 매핑이 없어야 범위를 벗어난 접근이 양쪽에서 같은 폴트가 된다(`/proc/self/maps`로 확인).
* **세그먼트**: Linux i386 사용자 프로세스는 CS = 0x23, DS/ES/SS = 0x2B, FS = 0(null), GS = TLS 선택자다. FS를 통한 접근은 #GP, CS override로 쓰기는 #GP다(코드 세그먼트는 쓰기 불가). GS의 base는 TLS 주소라 알 수 없다.
* **명령 인코딩**: 32비트 정수 인코딩은 x86-64에서 뜻이 달라(0x40~0x4F가 REX, PUSH/POP 폭) i386 프로세스가 필요하다. x87 인코딩은 두 모드에서 같다.

*Run the handler on an alternate stack, since the tested instruction owns ESP. Return through `siglongjmp` from a `sigsetjmp(env, 1)` that saved the signal mask; for a REP iteration trap, return from the handler instead to let the next iteration run. Signal delivery clears TF and DF, but AC and NT set by the instruction may survive: clear them right after the longjmp, or the next unaligned access raises #AC. At CPL 3, POPF silently keeps IF and IOPL. A region at a fixed low address (`MAP_FIXED_NOREPLACE`, at or above `vm.mmap_min_addr`, usually 4096 or 65536) lets the emulator use the same guest addresses so register values compare directly; nothing else may be mapped below or around it, so stray accesses fault on both sides (check `/proc/self/maps`). Linux i386 user processes run with CS = 0x23, DS/ES/SS = 0x2B, FS = 0 (null) and GS = the TLS selector: an FS access is #GP, a write through a CS override is #GP, and GS's base is unknown. 32-bit integer encodings mean something else on x86-64 (REX, PUSH/POP widths), so they need an i386 process; x87 encodings mean the same in both modes.*
