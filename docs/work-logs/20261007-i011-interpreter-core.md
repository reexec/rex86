# #11 작업 로그 : 인터프리터 1차 / #11 work log : interpreter increment 1

이슈: [#11](https://github.com/reexec/rex86/issues/11) | 설계: [20261007-i011](../design/20261007-i011-interpreter-core.md) | 지시서: [20261007-i011](../work-orders/20261007-i011-interpreter-core.md)

## 2026-10-07

- **구현**
  - `src/interp/`: `interpreter.{h,cpp}`(한 명령 실행 `Step`, 외부 인터럽트 진입 `EnterInterrupt`), `access.{h,cpp}`(레지스터 파일 매핑, 유효 주소 생성과 주소 크기 wrap, 세그먼트 base/limit과 폴트, `kTranslated` store 검사, 스택 push/pop), `flags.h`(폭별 즉시 계산). 1차 명령 그룹은 설계 결정 3 그대로. 미구현 mnemonic은 `StepStatus::kUnimplemented`로 구분되고 코어 밖에는 `kIllegalInstruction` 폴트로 보고된다.
  - `src/cpu.cpp`: `Run`이 실행 루프(정지 요청, 게이트, IF 경계 인터럽트 전달, 예산)가 되고 `ActiveEngine`은 `kInterpreter`. 공개 헤더 무변경.
  - `rex86_sst --execute`: 16 MiB 하네스, 초기 상태 적용 → **HLT retire까지 실행** → `RM32` 마스크 하 FINA 비교, 건드린 주소만 복원. 건너뜀 분류: 미구현, 예외 기록, 호스트 경계(INT/포트), 16 MiB 밖(24비트 버스 wrap), 하드웨어 quirk.
- **실측으로 잡은 것들** (전체 내용은 [analysis](../analysis/singlesteptests-386ex-deviations.md))
  1. 최종 상태는 주입된 HLT까지 실행된 상태다(분기 목적지에도 HLT). 러너를 "HLT retire까지"로 고쳐 첫 대량 불일치(전 테스트 EIP +1)가 풀렸다.
  2. 16비트 코드에서 0xFFFF의 명령을 실행하면 EIP는 wrap 없이 0x10000이 된다. fallthrough EIP 마스킹을 제거했다(`C3.MOO` 135건 해결).
  3. POPAD는 16비트 스택에서 ESP 상위 절반을 스택 이미지에서 적재한다(SDM의 "버린다"보다 정밀한 의미로 코어에 반영, `6661.MOO` 해결).
  4. 리그 디스크립터는 'unreal'(4 GiB limit), 물리 버스는 24비트 wrap(하네스에서 각각 4 GiB limit과 `skipped_unrepresentable`로 처리).
  5. **386EX quirk**: SIB index=none에 scale≠0이면 EA = base×scale+disp(SDM은 scale 무시). 수치 검증 세 건. 코어는 SDM을 따르고 러너가 `skipped_hw_quirk`로 분리한다(전체 6,883건).
  6. **Zydis 해석 차이**: 간접 CALL/JMP 앞 0x3E를 Zydis는 CET notrack으로 읽는다. IA-32에서는 DS override이므로 `SegmentOf`가 raw prefix를 직접 훑는다(`FF.2/FF.4` 각 28건 해결).
  7. 테스트 초기 eflags에 386에 없는 비트(18~23)가 섞여 있어 하네스가 `0x0003FFFF`로 마스킹한다(`669C` pushfd 979건 해결).
  8. 간접 near CALL의 push/read 순서를 구분하는 테스트는 없음을 확인하고 SDM 순서를 유지했다.
- **검증**
  - 단위 테스트: `interp_test.cpp` 추가(ALU 플래그 경계, 16/32비트 피연산자, 스택·call/ret, 분기 루프, 메모리 폴트와 EIP 보존, 게이트, 예산, HLT/INT/포트 이벤트, SMC 보고, RDTSC/CPUID, 미구현 보고). `rex86_unit_tests` checks 279 failures 0.
  - **SST 실행 비교(최종)**: 941개 파일 1,758,700개 테스트 — **executed=838,271, passed=838,271, mismatches=0**. skipped_unimplemented=757,242(시프트·곱셈/나눗셈·문자열·far·세그먼트 적재·x87 등 후속 증분), skipped_exception=150,304, skipped_boundary=6,000, skipped_hw_quirk=6,883.
  - 로컬 Windows x86(MSVC `-A Win32`, 경고를 오류로) 빌드 깨끗. 나머지 호스트는 push 시 CI.
- **남은 것**: 후속 증분(시프트/회전, MUL/DIV, 문자열+REP, far 제어 흐름, 세그먼트 적재, BT/SETcc/CMOVcc, x87)은 이 이슈의 코멘트 또는 새 작업으로. 예외 의미 비교(EXCP 테스트)와 폴트 종류 정밀화(fetch limit의 #GP 등)도 후속.

*Implementation: `src/interp/` holds `Step` and `EnterInterrupt`, the access layer (register mapping, effective-address generation with address-size wrap, segment base/limit faults, the `kTranslated` store check, stack ops) and the eager width-parametric flags; `Cpu::Run` became the loop (stop requests, gates, IF-boundary interrupt delivery, budget) answering `kInterpreter`, with no public-header change; `rex86_sst --execute` applies the initial state, runs **until the injected HLT retires** and compares FINA under the RM32 masks, reverting only touched addresses, with skips classified as unimplemented, exception, host-boundary, unrepresentable (24-bit bus wrap) and hardware quirk. Facts caught empirically (full detail in the analysis topic): the final state includes the injected HLT with HLTs seeded at branch targets; EIP passes 0xFFFF unwrapped in 16-bit code; POPAD loads ESP's upper half from the stack image under a 16-bit stack (adopted as the more precise semantics); the rig's descriptors are 'unreal' and its bus wraps at 24 bits (harness-side handling); the 386EX computes EA = base×scale+disp for SIB index=none with nonzero scale, which the core does not adopt (SDM rules) and the runner skips as `skipped_hw_quirk` (6,883); Zydis reads 0x3E before indirect CALL/JMP as CET notrack where IA-32 means a DS override, so `SegmentOf` scans raw prefixes; initial test eflags carry bits the 386 lacks, masked in the harness; and no test distinguishes an indirect call's push/read order, so the SDM order stays. Verification: 279 unit checks 0 failures including the new interpreter tests; the final SST execute-and-compare over all 941 files and 1,758,700 tests shows **838,271 executed, 838,271 passed, zero mismatches** with the skips as listed; the local Windows x86 build is clean and CI checks the other hosts on push. Remaining: the later instruction increments (shifts/rotates, MUL/DIV, strings+REP, far control flow, segment loads, BT/SETcc/CMOVcc, x87), exception-semantics comparison and fault-kind precision follow as comments on this issue or new tasks.*
