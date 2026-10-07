# #15 작업 지시 : 인터프리터 3차 / #15 work order : interpreter increment 3

이슈: [#15](https://github.com/reexec/rex86/issues/15) | 설계: [20261007-i015](../design/20261007-i015-interpreter-increment-3.md) | 로그: [20261007-i015](../work-logs/20261007-i015-interpreter-increment-3.md)

## 작업 항목 / Tasks

1. `src/interp/access.{h,cpp}`: `LoadSegment`(설계 결정 1).
2. `src/interp/exec_segments.cpp`: 세그먼트 적재 명령, far JMP/CALL/RETF, IRET, WAIT, CLTS 등 특권 명령. `src/interp/exec_bcd.cpp`: BCD, BOUND, SALC. `exec_strings.cpp`: INS/OUTS(+REP, 결정 4).
3. `interpreter.{h,cpp}`: `StepStatus::kStopped`, `StepResult::inhibit_interrupts`, MOV/PUSH/POP sreg와 far 분기의 위임.
4. `include/rex86/cpu.h`, `src/cpu.cpp`: private `interrupt_shadow_`, `kStopped` 처리. `include/rex86/environment.h`: `LoadDescriptor` 주석에 결정 1의 의미.
5. `rex86_sst`: `PortWrite` 수락, `kStopped`/특권 폴트의 경계 분류.
6. 단위 테스트(설계 검증 절), SST 전체 실행, 문서(ARCHITECTURE, README 달성도, analysis 필요 시).

*1 add `LoadSegment`; 2 implement the segment-load instructions, far JMP/CALL/RETF, IRET, WAIT and the privileged instructions in `exec_segments.cpp`, BCD/BOUND/SALC in `exec_bcd.cpp`, and INS/OUTS with REP in `exec_strings.cpp`; 3 add `StepStatus::kStopped` and `StepResult::inhibit_interrupts` and route MOV/PUSH/POP sreg and the far branches; 4 add `Cpu`'s private shadow and `kStopped` handling and document decision 1 in `environment.h`; 5 make the harness accept port writes and classify the new boundaries; 6 unit tests, the full SST run and the documents.*

## 완료 조건 / Completion criteria

* 전체 SST mismatches 0, 미구현 건수 대폭 감소(남는 것은 근거와 함께 로그에).
* 단위 테스트 전체 통과, 로컬 Windows x86 빌드 깨끗. 공개 헤더 변경은 설계에 적은 두 가지(주석, private 멤버)뿐.

*Zero SST mismatches with the unimplemented count sharply down (whatever remains is logged with reasons); all unit tests pass on a clean local Windows x86 build; the only public-header changes are the two the design names (a comment and a private member).*
