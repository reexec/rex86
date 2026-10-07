# #15 작업 로그 : 인터프리터 3차 / #15 work log : interpreter increment 3

이슈: [#15](https://github.com/reexec/rex86/issues/15) | 설계: [20261007-i015](../design/20261007-i015-interpreter-increment-3.md) | 지시서: [20261007-i015](../work-orders/20261007-i015-interpreter-increment-3.md)

## 2026-10-07

- **범위 결정**: #13 뒤 미구현 129,985건을 파일별로 집계해 범위를 정했다 — 세그먼트 적재 약 69,000, far 제어 흐름 약 28,700, BCD 약 14,900, INS/OUTS·BOUND·WAIT·SALC·CLTS 약 17,000. 스위트에 x87 테스트는 없다.
- **구현**
  - `access`: `LoadSegment` — 모든 선택자(null 포함)에 `LoadDescriptor`를 호출하고 CS는 present+executable, SS는 present+writable을 요구. 거절 시 `kGeneralProtection`, 대상 레지스터 무변경.
  - `exec_segments.cpp`: MOV sreg, PUSH/POP sreg, LDS/LES/LFS/LGS/LSS, far JMP/CALL/RETF(같은 권한), IRET, WAIT, 특권 명령(CLTS, LGDT, LIDT, LLDT, LTR, LMSW, INVD, WBINVD, INVLPG)의 `kPrivilegedInstruction` 거절. CS 적재가 거절되면 이미 바꾼 ESP를 되돌린다.
  - `exec_bcd.cpp`: AAA/AAS/DAA/DAS/AAM(0이면 `kDivide`)/AAD, BOUND(`kBound`), SALC.
  - `exec_strings.cpp`: INS/OUTS+REP. 호스트가 거절하면 retire 없이 재시작 가능하게 멈춘다(`StepStatus::kStopped`).
  - 인터럽트 shadow: `StepResult::inhibit_interrupts`와 `Cpu`의 private `interrupt_shadow_`(MOV SS, POP SS, IF를 켠 STI).
  - 공개 헤더 변경은 설계대로 두 가지: `cpu.h`의 private 멤버, `environment.h`의 `LoadDescriptor` 의미 주석. API 무변경.
  - SST 하네스: `PortWrite` 수락(OUT/OUTS가 비교 대상이 됨), `kStopped`·특권 폴트는 경계로 분류.
- **SST가 잡은 것**: AAS를 "AL−6, AH−1"로 구현했던 것이 169건 불일치로 드러났다. SDM 의사코드는 `AX ← AX − 6`(16비트, AL의 borrow가 AH로 전파) 후 `AH ← AH − 1`이고, 386도 그대로다(`3F.MOO` #31: AX=0xE305 → 0xE10F). 의사코드대로 고쳐 해결. SDM과 다른 하드웨어 동작은 이번 증분에서 새로 나오지 않았다(PUSH sreg의 32비트 슬롯, far CALL의 CS 슬롯도 0 확장으로 일치).
- **검증**
  - 단위 테스트 8개 블록 추가(세그먼트 적재 거절/성공, CS 실행 가능 검사, far CALL/RETF 왕복, IRET, STI·MOV SS shadow, 거절된 REP OUTSB 재시작, DAA/AAM, BOUND/SALC). checks 343 failures 0.
  - **SST 실행 비교(최종)**: 941 파일 1,758,700 테스트 — **executed=1,592,800, passed=1,592,800, mismatches=0, skipped_unimplemented=0**. 나머지 건너뜀: 예외 150,304, 경계 8,701(INT n, 포트 입력, 특권 명령), 표현 불가 3, 하드웨어 quirk 6,892.
  - 로컬 Windows x86(MSVC, 경고를 오류로) 빌드 깨끗. 나머지 호스트는 push 시 CI.
- **의미**: 정수 명령은 SST가 덮는 범위에서 완결되었다. 다음은 예외 의미 비교(15만 건, real mode IVT 전달을 하네스가 흉내), x87, 호스트 CPU 대조 fuzz와 trace.

*Scope came from a per-file tally of the 129,985 unimplemented tests after #13 (segment loads ~69,000, far control flow ~28,700, BCD ~14,900, INS/OUTS/BOUND/WAIT/SALC/CLTS ~17,000; the suite has no x87). Implementation: `LoadSegment` asks `LoadDescriptor` for every selector, null included, requiring present+executable for CS and present+writable for SS, refusing with kGeneralProtection and leaving the register unchanged; `exec_segments.cpp` holds MOV/PUSH/POP sreg, the far-pointer loads, same-privilege far JMP/CALL/RETF, IRET, WAIT and the refusal of the system instructions with kPrivilegedInstruction, restoring ESP when a CS load is refused; `exec_bcd.cpp` holds the BCD adjustments, BOUND and SALC; `exec_strings.cpp` gains INS/OUTS with REP stopping restartably (`StepStatus::kStopped`) when the host declines; the interrupt shadow lives in `StepResult::inhibit_interrupts` and `Cpu`'s private `interrupt_shadow_`. The only public-header changes are the design's two (the private member and the `LoadDescriptor` comment). The harness accepts port writes, making OUT/OUTS comparable, and classifies kStopped and privileged faults as boundaries. SST caught AAS implemented as "AL−6, AH−1": the SDM pseudocode — and the 386 — subtract 6 from AX as a 16-bit value (AL's borrow reaching AH) before AH−1; fixed per the pseudocode. No new SDM deviation appeared (the 32-bit PUSH sreg slot and far CALL's CS slot match zero extension). Verification: eight new unit blocks, checks 343 failures 0; the final SST execute-and-compare over all 941 files — **1,592,800 executed, 1,592,800 passed, zero mismatches, zero unimplemented** — with 150,304 exception, 8,701 boundary, 3 unrepresentable and 6,892 quirk skips; a clean local Windows x86 build, CI for the other hosts on push. The integer set is complete as far as the suite reaches; next come the exception-semantics comparison (150,000 tests, with the harness emulating real-mode IVT delivery), x87, and the host-CPU comparison fuzz with traces.*
