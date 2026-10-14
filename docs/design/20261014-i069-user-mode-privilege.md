# #69 설계 : 사용자 모드 특권 의미(CPL 3, IOPL 0) / #69 design : user-mode privilege semantics (CPL 3, IOPL 0)

이슈: [#69](https://github.com/reexec/rex86/issues/69) | 지시서: [20261014-i069](../work-orders/20261014-i069-user-mode-privilege.md) | 로그: [20261014-i069](../work-logs/20261014-i069-user-mode-privilege.md)

## 배경 / Background

rePIU는 rex86을 세 번째 실행 모델 `emulated`로 붙인다([rePIU #61](https://github.com/reexec/rePIU/issues/61), [설계](https://github.com/reexec/rePIU/blob/main/docs/design/20261014-i061-emulated-execution-model.md)). rePIU의 HLE는 네이티브 Win32 프로세스에서 게스트가 사용자 모드에 허용되지 않는 일을 해 폴트가 나면, EIP의 바이트를 디코드해 그 명령을 에뮬레이션한다(`INT 21h`, `CLI`/`STI`, `IN`/`OUT`, DPMI 셀렉터 적재 등). 그래서 rex86이 같은 명령에서 같은 종류의 폴트를 **실행 전에** 내면 HLE를 고치지 않고 붙일 수 있다.

*rePIU attaches rex86 as a third execution model, `emulated` (rePIU #61). rePIU's HLE, in a native Win32 process, decodes the bytes at EIP and emulates the instruction whenever the guest faults by doing something user mode forbids (`INT 21h`, `CLI`/`STI`, `IN`/`OUT`, DPMI selector loads and so on). So if rex86 raises the same kind of fault on the same instructions **before retiring them**, the HLE attaches unchanged.*

지금의 코어는 CPL 0처럼 동작한다. 단일 권한 수준을 가정하고, 시스템 명령(`LGDT` 등)만 거절한다.

*Today the core behaves like CPL 0: it assumes a single privilege level and refuses only the system instructions (`LGDT` and the like).*

| 명령 / Instruction | 지금 / Today | 사용자 모드 프로세스 / A user-mode process |
|---|---|---|
| `CLI`, `STI` | IF를 바꾸고 이벤트 없음 / changes IF, no event | #GP (IOPL < CPL) |
| `HLT` | 실행 뒤 `kHalted` / `kHalted` after retiring | #GP (CPL > 0) |
| `IN`, `OUT`, `INS`, `OUTS` | 호스트에 묻고, 거절되면 `kPortIo` / asks the host, `kPortIo` when declined | #GP (I/O 권한 비트맵 없음 / no I/O permission bitmap) |
| `INT imm8` | 실행 뒤 `kSoftwareInterrupt` / after retiring | #GP(벡터×8+2) (게이트 DPL < CPL / gate DPL below CPL) |
| `POPF`, `IRET` | IF, IOPL을 바꿈 / change IF and IOPL | IF, IOPL 그대로, 폴트 없음 / left alone, no fault |

## 결정 1: `Features::user_mode` / Decision 1: `Features::user_mode`

`Features`에 `user_mode`(기본 `false`)를 더한다. 켜면 코어는 **CPL 3, IOPL 0, IDT 게이트가 모두 DPL 0인 보호 모드 프로세스**처럼 위 표의 오른쪽 열을 따른다. 이것은 Intel SDM이 정한 실제 동작을 하나 더 모델링하는 것이지 우회 장치가 아니다. 기본값이 꺼짐이므로 기존 테스트, trace, fuzz, re2DJ에는 영향이 없다.

*`Features` gains `user_mode` (default `false`). When on, the core follows the right-hand column above, as **a protected-mode process at CPL 3 with IOPL 0 whose IDT gates are all DPL 0**. This models one more piece of the behaviour the Intel SDM defines rather than adding a workaround. Being off by default, it leaves the existing tests, traces, fuzz and re2DJ untouched.*

`Features`에 둔 이유: 실행 중에 바뀌지 않는 CPU의 성질이고, 명령 실행 경로가 이미 `Features`를 받는다. CPL과 IOPL을 `CpuState`의 값으로 모델링하는 것(권한 전환, 호출 게이트)은 사용자 모드 코어의 범위 밖이다.

*Why `Features`: it is a property of the CPU that does not change during execution, and the execution path already receives `Features`. Modelling CPL and IOPL as `CpuState` values (privilege transitions, call gates) is outside a user-mode core's scope.*

## 결정 2: 폴트의 모양 / Decision 2: the shape of the faults

| 명령 / Instruction | `FaultKind` | `fault_address` |
|---|---|---|
| `CLI`, `STI`, `HLT`, `IN`, `OUT`, `INS*`, `OUTS*` | `kPrivilegedInstruction` | 명령의 선형 주소 / the instruction's linear address |
| `INT imm8` | `kGeneralProtection` | 오류 코드 벡터×8+2 / error code vector×8+2 |

* 특권 명령은 기존 시스템 명령 거절(`exec_segments.cpp`)과 같은 종류와 주소 규약을 쓴다. Windows가 이 경우 `STATUS_PRIVILEGED_INSTRUCTION`을 내는 것과도 맞는다.
* `INT imm8`의 #GP는 SDM의 오류 코드(IDT 비트 = 2)를 `LoadSegment`의 #GP가 셀렉터를 싣는 자리에 싣는다.
* 모두 **실행 전**이다. EIP는 그 명령을 가리키고, 정수 상태는 그대로이며, 포트는 건드리지 않는다(`Environment::PortRead/Write`를 부르지 않음). REP `INS`/`OUTS`도 첫 반복 전에 폴트한다.
* `INT3`(`CC`), `INTO`, `INT1`(`F1`)은 그대로 둔다. 사용자 모드 운영체제는 이 벡터들을 DPL 3 게이트로 열어 두고, 호스트는 지금처럼 `kSoftwareInterrupt`로 받는다. `CD 03`도 `INT imm8`이므로 #GP다. 다른 DPL 3 벡터(예: Windows의 `2Eh`)가 필요한 호스트는 #GP 처리에서 오류 코드로 구분한다.

*Privileged instructions use the kind and address convention of the existing system-instruction refusal (`exec_segments.cpp`), which also matches Windows' `STATUS_PRIVILEGED_INSTRUCTION`. `INT imm8`'s #GP carries the SDM's error code (IDT bit = 2) where `LoadSegment`'s #GP carries the selector. All of them happen **before retiring**: EIP addresses the instruction, the integer state is unchanged and no port is touched (`Environment::PortRead/Write` is not called); a REP `INS`/`OUTS` faults before its first iteration. `INT3` (`CC`), `INTO` and `INT1` (`F1`) stay as they are: a user-mode OS opens those vectors with DPL 3 gates, and the host keeps receiving `kSoftwareInterrupt`. `CD 03` is an `INT imm8` and so a #GP; a host needing another DPL 3 vector (such as Windows' `2Eh`) tells it apart by the error code in its #GP handling.*

## 결정 3: 검사 위치 / Decision 3: where the check sits

검사는 `interp::ExecuteDecoded`에서 `FeatureEnabled` 바로 뒤에 둔다. 단일 스텝, 블록 루프, 디코드 캐시 적중이 모두 이 함수를 지나고, 이 자리는 상태 저장 전이라 실행 전 폴트가 자연스럽다. 명령 분류는 mnemonic 하나로 끝나므로 비용은 켜진 경우에도 switch 하나다.

*The check sits in `interp::ExecuteDecoded` right after `FeatureEnabled`. Single steps, the block loop and decode-cache hits all pass through it, and it comes before the state is saved, so a fault before retiring is natural. The classification is by mnemonic alone, so even when on it costs one switch.*

`POPF`/`IRET`의 IF·IOPL 마스크는 각 구현의 쓰기 마스크에서 `user_mode`일 때 `IF | IOPL`을 뺀다.

*For `POPF`/`IRET`, the write mask of each implementation drops `IF | IOPL` under `user_mode`.*

번역 경로는 이 명령들을 번역하지 않는다(IR 프런트엔드가 블록을 끝내고 인터프리터로 넘긴다). 그래서 번역 쪽 변경은 없다. 이것은 테스트로 고정한다.

*The translation path does not translate these instructions (the IR frontend ends the block and hands them to the interpreter), so translation needs no change. A test pins that.*

## 소비자 영향 / Effect on consumers

* rePIU: `emulated` 모델이 `user_mode = true`로 코어를 만든다. 어댑터가 `kPrivilegedInstruction`과 `kGeneralProtection`을 rePIU의 `FaultEvent`로 바꾼다.
* re2DJ: 바뀌지 않는다(기본 꺼짐). Win32 게스트이므로 같은 옵션을 쓸 수 있다.

*rePIU: the `emulated` model creates the core with `user_mode = true`, and its adapter turns `kPrivilegedInstruction` and `kGeneralProtection` into rePIU's `FaultEvent`. re2DJ: unchanged (off by default); being a Win32 guest, it can use the same option.*

## 검증 / Verification

* 단위 테스트 `user_mode_test.cpp`: 각 명령이 실행 전에 폴트하고 EIP와 정수 상태가 그대로인지, 포트가 불리지 않는지, `INT 21h`의 오류 코드, `INT3` 불변, `POPF`/`IRETD`의 IF·IOPL 보존과 다른 플래그 반영, 꺼진 경우의 기존 동작.
* 기존 테스트와 probe, 다섯 호스트 CI.

*Unit tests in `user_mode_test.cpp`: each instruction faults before retiring with EIP and the integer state unchanged and no port call, `INT 21h`'s error code, `INT3` unchanged, `POPF`/`IRETD` keeping IF and IOPL while applying the other flags, and the existing behaviour with the option off. The existing tests and probes on the five-host CI.*
