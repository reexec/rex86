# #15 설계 : 인터프리터 3차 — 세그먼트 적재, far 제어 흐름, BCD, 나머지 정수 명령 / #15 design : interpreter increment 3 — segment loads, far control flow, BCD and the remaining integer instructions

이슈: [#15](https://github.com/reexec/rex86/issues/15) | 지시서: [20261007-i015](../work-orders/20261007-i015-interpreter-increment-3.md) | 로그: [20261007-i015](../work-logs/20261007-i015-interpreter-increment-3.md)

## 범위 / Scope

#13 이후 SST 미구현 129,985건의 실측 분포(파일별 집계)가 이 작업의 범위를 정한다. 이 스위트에는 x87 테스트가 없으므로 정수 명령은 이 증분으로 SST 위에서 완결된다.

| 그룹 | 명령 | 미구현 건수 |
|---|---|---|
| 세그먼트 적재 | LDS/LES/LFS/LGS/LSS, MOV sreg,r/m, PUSH/POP sreg | 약 69,000 |
| far 제어 흐름 | far JMP(EA, FF/5), far CALL(9A, FF/3), RETF(CA, CB), IRET | 약 28,700 |
| BCD | AAA/AAS/DAA/DAS/AAM/AAD | 약 14,900 |
| 그 밖 | INS/OUTS(+REP), BOUND, WAIT, SALC, CLTS | 약 17,000 |

## 결정 1: 세그먼트 적재는 항상 호스트에게 묻는다 / Decision 1: every segment load asks the host

`LoadSegment(ctx, segment, selector)`(access 계층)가 유일한 경로다. 선택자 값과 무관하게(null 포함) `Environment::LoadDescriptor`를 호출하고, false면 `kGeneralProtection` 폴트(명령 retire 없음, 대상 레지스터 무변경)다. null 선택자의 의미(보호 모드의 "적재는 허용, 사용 시 폴트")는 호스트가 `present=false` 디스크립터로 표현한다. 코어가 null을 특별 취급하지 않아야 real mode 하네스(선택자 0 = base 0)와 보호 모드 소비자가 같은 코어를 쓴다.

코어가 검사하는 것은 레지스터 역할에 따른 최소 조건뿐이다:

* CS(far JMP/CALL/RETF/IRET): `present && executable`이 아니면 `kGeneralProtection`.
* SS: `present && writable`이 아니면 `kGeneralProtection`.
* DS/ES/FS/GS: 검사 없음(`present=false`도 적재; 사용 시 기존 `Linearize`가 폴트).

**소비자 영향**: 공개 API는 바뀌지 않지만 `LoadDescriptor` 계약의 의미가 구체화된다 — 코드 선택자에는 `executable=true`를, 스택 선택자에는 `writable=true`를 돌려줘야 하고, null 선택자에도 호출된다. rePIU(SelectorTable)와 re2DJ(TEB/평탄 선택자)의 어댑터는 2A/2B 단계에서 이 규칙대로 구현한다. `environment.h`의 주석을 이 의미로 갱신한다.

*`LoadSegment` in the access layer is the only path: it calls `Environment::LoadDescriptor` for every selector, null included, and a false answer is a `kGeneralProtection` fault that retires nothing and leaves the target register unchanged. The null selector's protected-mode meaning (loadable, faulting on use) is the host's to express as a `present=false` descriptor; the core must not special-case null, or the real-mode harness (selector 0 = base 0) and the protected-mode consumers could not share it. The core checks only role minima: CS needs present and executable, SS present and writable, DS/ES/FS/GS nothing (a not-present segment faults on use through the existing `Linearize`). Consumer impact: no API change, but the `LoadDescriptor` contract becomes concrete — code selectors must answer `executable=true`, stack selectors `writable=true`, and null selectors are asked too; rePIU's and re2DJ's adapters implement this in phases 2A/2B, and the comment in `environment.h` is updated to say so.*

## 결정 2: 인터럽트 지연(shadow) / Decision 2: the interrupt shadow

SDM대로 MOV SS, POP SS 직후와 IF를 0→1로 바꾼 STI 직후의 명령 경계에서는 외부 인터럽트를 전달하지 않는다(스택 전환 `mov ss / mov esp` 쌍과 `sti; hlt` 관용구의 정확성). `StepResult`에 `inhibit_interrupts`를 두고 `Cpu`가 private 멤버로 한 경계 동안 기억한다.

**공개 헤더 변경**: `cpu.h`에 private 멤버 `interrupt_shadow_` 하나가 더해진다. API와 의미 계약은 바뀌지 않으며, 소비자는 FetchContent로 다시 빌드할 뿐이다(어댑터 코드 변경 없음).

*Per the SDM, an external interrupt is not delivered at the boundary right after MOV SS, POP SS, or an STI that set IF from 0 — what makes `mov ss / mov esp` pairs and `sti; hlt` correct. `StepResult` gains `inhibit_interrupts` and `Cpu` remembers it for one boundary in a new private member. Public header change: `cpu.h` gains the private member `interrupt_shadow_`; the API and semantic contract are unchanged and consumers simply rebuild through FetchContent with no adapter change.*

## 결정 3: far 제어 흐름은 같은 권한 수준만 / Decision 3: far control flow at one privilege level

소비자는 모두 단일 권한(ring 3 또는 real/DPMI 평탄)에서 돈다. 따라서 far JMP/CALL/RETF/IRET는 "CS 적재 + EIP 설정 + (CALL/RETF/IRET의) 스택 프레임"으로 구현하고, 콜 게이트·태스크 게이트·권한 전환·스택 전환은 범위 밖이다(필요해지면 호스트가 그런 선택자를 게이트 주소로 돌려주어 `kGate`로 받는다). 프레임 폭은 피연산자 크기, CS 슬롯은 32비트 프레임에서 선택자를 0 확장한 값이다(386 실측과 다르면 analysis에 기록). IRET은 EIP·CS·EFLAGS를 꺼내며 EFLAGS 쓰기 마스크는 POPF와 같다.

*All consumers run at a single privilege level, so far JMP/CALL/RETF/IRET are "load CS, set EIP, and (for CALL/RETF/IRET) the stack frame"; call gates, task gates, privilege and stack switches are out of scope (a host needing one maps the selector to a gate address and receives `kGate`). The frame width is the operand size and a 32-bit frame's CS slot holds the zero-extended selector (any 386 deviation goes to the analysis topic); IRET pops EIP, CS and EFLAGS with POPF's write mask.*

## 결정 4: 문자열 포트 I/O는 재시작 가능한 정지 / Decision 4: string port I/O stops restartably

INS/OUTS(+REP)는 반복마다 `PortRead/PortWrite`를 부른다. 호스트가 거절하면 그 반복 직전 상태(완료된 반복의 SI/DI/CX 갱신은 아키텍처 상태)에서 **retire 없이** `kPortIo`로 멈춘다. 재개하면 남은 횟수로 다시 실행된다 — 하드웨어의 재시작 가능한 REP 의미 그대로다. 이를 위해 `StepStatus::kStopped`(retire 없는 정지)를 더한다. 단일 IN/OUT은 #11 그대로(retire 후 정지).

*INS/OUTS with REP call `PortRead/PortWrite` per iteration; a declined access stops with `kPortIo` **without retiring**, at the state before that iteration (completed iterations' SI/DI/CX updates are architectural), and resumption re-executes with the remaining count — the hardware's restartable REP. `StepStatus::kStopped` (stopped, not retired) is added for this; single IN/OUT keep #11's retire-then-stop.*

## 결정 5: 그 밖의 명령 / Decision 5: the rest

* **BCD**: SDM 의사코드 그대로(DAS의 두 번째 조건에 ELSE가 없는 점 포함). AAM 0은 `kDivide`. 미정의 플래그는 f_umask가 가리고, 가리지 않는 불일치가 나오면 386 실측값을 analysis에 기록하고 반영한다(#13 원칙).
* **BOUND**: 부호 있는 범위 밖이면 `kBound` 폴트(retire 없음).
* **WAIT/FWAIT**: x87 상태 워드의 ES(비트 7)가 꺼져 있으면 no-op. 켜져 있으면 `kOther` 폴트(x87 증분에서 정밀화).
* **SALC**(D6, 문서화 안 된 386 명령): AL = CF ? 0xFF : 0x00.
* **CLTS** 등 특권 명령: 사용자 모드 코어이므로 `kPrivilegedInstruction` 폴트. 하네스는 경계로 분류한다.

## SST 하네스 변경 / Harness changes

* `PortWrite`를 수락해 OUT/OUTS 테스트를 비교 대상으로 만든다(`PortRead`는 계속 거절 — 리그의 버스 입력값을 재현할 근거가 없음).
* `kStopped`와 `kPrivilegedInstruction`은 경계로 분류.

## 검증 / Verification

* 단위 테스트: 세그먼트 적재 성공/거절(레지스터 무변경), CS/SS 역할 검사, far CALL/RETF 왕복, IRET, MOV SS 인터럽트 shadow, 거절된 REP OUTS의 재시작, BCD 대표값, BOUND 폴트, SALC.
* SST 실행 비교 전체 스위트 mismatches 0 유지, 미구현이 x87 외 0에 가까워지는지 기록.
* 로컬 Windows x86 빌드와 `ctest`, 나머지 호스트는 push 시 CI.

*BCD follows the SDM pseudocode (including DAS's missing ELSE), AAM 0 faults kDivide, and unmasked undefined-flag mismatches adopt the measured 386 value per #13's rule; BOUND faults kBound; WAIT is a no-op unless the x87 status word's ES bit is set (then kOther, refined with x87); SALC sets AL from CF; CLTS and other privileged instructions fault kPrivilegedInstruction in a user-mode core. The harness accepts port writes so OUT/OUTS tests become comparable (port reads stay declined — nothing reproduces the rig's bus input) and classifies kStopped and privileged faults as boundaries. Verification: unit tests for segment-load success/refusal, the CS/SS role checks, far CALL/RETF round trips, IRET, the MOV SS shadow, restartable declined REP OUTS, BCD values, the BOUND fault and SALC; zero SST mismatches with the remaining unimplemented count recorded; the local build and ctest, CI for the other hosts.*
