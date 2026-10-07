# SingleStepTests 386EX 실측에서 확인된 사실 / Facts measured from the SingleStepTests 386EX suite

근거 작업: [#11](https://github.com/reexec/rex86/issues/11) ([로그](../work-logs/20261007-i011-interpreter-core.md)) | 데이터: [SingleStepTests/80386](https://github.com/SingleStepTests/80386) v1 real mode, 941 파일 | 도구: `rex86_sst --execute`

상태 표기: **확인됨**(이 스위트의 수치로 검증), **추정**(일관되지만 별도 확인 없음).

## 1. 테스트 구조 / Test structure

* **확인됨**: `BYTS`는 테스트 대상 명령 + 생성기가 경계 검출용으로 주입한 HLT(0xF4) 1바이트다(`EB.MOO` 첫 테스트의 바이트 `EB 87 F4`). 분기 목적지에도 HLT가 심어져 있어, 최종 상태는 **HLT가 retire된 뒤**의 상태다(분기 테스트의 기대 EIP = 목적지 + 1).
* **확인됨**: real mode 스위트지만 리그의 세그먼트 디스크립터 캐시는 'unreal'이다. 64KiB를 넘는 32비트 유효 주소 접근이 예외 기록 없이 수행된다(`6784.MOO` #1076 `test [ds:eax-183Bh],bl` 등). 세그먼트 limit을 0xFFFF로 모델링하면 이 테스트들이 #GP가 되므로, 비교 하네스는 4GiB limit을 쓴다.
* **확인됨**: 물리 버스는 24비트(16MiB)에서 wrap한다(`EA32`의 `p_addr`). 선형 16MiB 밖 접근은 평탄 하네스로 표현할 수 없어 건너뛰고 센다.
* **확인됨**: 레지스터 기록(SMM 덤프)의 EFLAGS는 386에 존재하지 않는 비트 18~31을 1로 보고한다(`FE.0.MOO` 기대값 `0xFFFC0403` 등). 아키텍처 레지스터는 그 비트를 0으로 push한다(`669C.MOO` PUSHFD 이미지의 상위 바이트 0x00). 따라서 하네스는 초기 eflags 적용을 `0x0003FFFF`로 마스킹하고 eflags 비교에서 `0xFFFC0000`을 무시한다.

## 2. SDM과 다른 하드웨어 동작 / Hardware behavior deviating from the SDM

이 항목들은 코어에 반영하지 않는다. AGENTS.md: 게스트 명령의 의미는 Intel SDM이 기준이고 하드웨어는 측정 도구다. 비교 하네스가 해당 인코딩을 `skipped_hw_quirk`로 분리한다.

* **확인됨 — SIB index=none에서 scale이 base에 적용된다.** SIB의 index 필드가 100(인덱스 없음)이고 scale≠0일 때, SDM은 scale을 무시하지만 386EX는 **EA = base × scale + disp**를 계산한다. 수치 검증 세 건:
  - `678D.MOO` #94 `lea bp,[esp-2]`(SIB `A4`: scale=4, index=100, base=ESP): ESP=8, 기대 BP=0x1E = 8×4−2.
  - `678D.MOO` #115 `lea ax,[esi+4Dh]`(SIB `E6`: scale=8): ESI 하위 0xA101, 기대 AX=0x0855 = (0xA101×8+0x4D) 하위 16비트.
  - `678D.MOO` #328 `lea bp,[esp+2275E981h]`(SIB `64`: scale=2): ESP=8, 기대 BP=0xE991 = 8×2+0xE981 하위 16비트.
  스위트의 `EA32` 기록 자체는 SDM 공식으로 계산되어 레지스터 결과와 불일치한다(업스트림 README의 "invalid SIB encodings produce incorrect effective address calculations in test output"과 부합).
* **확인됨 — POPAD가 ESP 상위 절반을 스택 이미지에서 적재한다.** SDM은 저장된 ESP를 버린다고 하지만, 16비트 스택(SS.D=0)에서 386EX의 최종 ESP는 상위 16비트 = 이미지 값, 하위 16비트 = 증가한 SP다(`6661.MOO` 전체). **이것은 코어에 반영했다**: 스택 폭이 지배하지 않는 비트는 이미지에서 온다는 규칙은 SDM의 "버린다"와 양립 가능한 해석이고, 32비트 스택에서는 SDM 그대로 동작한다.

## 2b. SDM이 미정의로 두는 곳의 386 실측값 / Measured 386 values where the SDM says undefined

이 항목들은 SDM이 **미정의**로 두는 곳이라 어떤 결정적 값도 규칙 위반이 아니다. #13은 게스트 충실도를 위해 아래 실측값을 코어에 반영했다(마스크가 없어 SST 비교가 이 값을 요구하기도 한다).

* **확인됨 — 시프트 count > 폭의 CF** (`D2.4/D2.5/D3.5`): 피연산자가 폭 주기로 재순환한다. count가 폭의 배수면 SHR의 CF = 부호 비트, SHL의 CF = 최하위 비트, 그 외 count > 폭은 CF = 0. (처음 세운 '32비트 복제' 가설은 (0xFE,10)→0, (0xFF,29)→0 반례로 기각.)
* **확인됨 — 16비트 SHLD/SHRD의 count > 16** (`0FA4.MOO` 5건 수치 검증): 결과 = **소스만의 회전**(SHLD는 `ROL16(src, count−16)`, SHRD는 `ROR16(src, count−16)`), 목적지 값은 무시된다. CF는 count ≤ 16과 같은 공식(연결값에서 마지막으로 밀려난 비트)이 그대로 성립.
* **확인됨, 미반영 — BT 계열의 OF** (`0FA3.MOO` 8건 수치 검증): OF = `ROR(dst, index)` 결과의 상위 두 비트 XOR(회전 명령의 OF 정의와 동일), SF/ZF/AF/PF는 보존. f_umask가 이 플래그들을 미정의로 가려 비교에 불요하므로 코어는 단순 보존을 유지한다.
* **미확정 — IDIV 음수 오버플로의 비폴트 완료** (`F6.7/F7.7` 9건): 몫이 범위를 벗어나는 일부 음수 몫 케이스에서 386EX가 #DE 없이 AL=0x80, AH=(피제수 − (−128)×제수)의 하위 바이트를 쓴다(수치 2건 검증). 폴트 술어는 샘플 부족으로 미확정. 코어는 SDM대로 #DE를 유지하고 비교 하네스가 `skipped_hw_quirk`로 분리한다.
* **확인됨 — 리그의 장주기 REP 중단**: 32비트 주소 REP에 초기 ECX가 큰 테스트는 리그가 중간에 중단한 부분 상태를 기록한다(아키텍처적으로 유효한 중간 상태). 하네스는 ECX > 0xFFFF인 a32 REP를 건너뛴다.

*Where the SDM says undefined, any deterministic value is legal; #13 adopted the measured 386 values below for guest fidelity (and because unmasked comparisons require them). Confirmed: for shift counts above the width the operand recirculates with the width as period — CF is the sign bit (SHR) or the low bit (SHL) at count multiples of the width and zero otherwise (the earlier 32-bit-replication hypothesis fell to counterexamples); 16-bit SHLD/SHRD above count 16 rotate the SOURCE alone (`ROL16/ROR16(src, count−16)`, destination ignored) with the count ≤ 16 CF formula still holding; the BT family's OF equals the rotate-style OF of `ROR(dst, index)` with SF/ZF/AF/PF preserved (verified 8/8 but NOT adopted — f_umask hides those flags, so the core keeps simple preservation). Unresolved: a handful of IDIV negative-overflow cases complete without #DE, writing AL=0x80 and the remainder taken at q=−128; the fault predicate lacks samples, the core keeps the SDM's #DE and the harness skips them as `skipped_hw_quirk`. Confirmed: long a32 REP tests record the rig's mid-string interruption (a valid architectural intermediate), so the harness skips a32 REPs with ECX > 0xFFFF.*

## 3. 인코딩 해석 주의 / Encoding interpretation notes

* **확인됨**: 간접 CALL/JMP 앞의 0x3E를 Zydis는 CET notrack 힌트로 해석해 `operand.mem.segment`에 반영하지 않는다. IA-32(이 코어의 게스트는 CET 이전)에서는 DS override다. 코어의 `SegmentOf`는 raw prefix를 직접 훑어 마지막 세그먼트 prefix를 적용한다(`FF.2.MOO` #87 `3E FF 13`: 하드웨어 l_addr = DS base + EA로 확인).
* **확인됨**: 간접 near CALL의 push/read 순서를 구분하는 테스트는 이 스위트에 없다. 3E override 수정 뒤 두 순서 모두 `FF.2.MOO` 2420/2420을 통과했다. 코어는 SDM 순서(피연산자 읽기 후 push)를 유지한다.

*Measured facts from the SingleStepTests 386EX real-mode suite. Structure (confirmed): `BYTS` is the tested instruction plus one injected HLT, HLTs are seeded at branch targets so the final state follows the HLT's retirement; the rig's descriptor caches are 'unreal' (32-bit EAs beyond 64 KiB execute without a recorded exception, so the comparison harness models 4 GiB limits); the physical bus wraps at 24 bits; and the SMM register dump reports the 386's nonexistent EFLAGS bits 18-31 as ones while the architectural register pushes them as zeros (PUSHFD image), so the harness masks the initial apply with 0x0003FFFF and ignores 0xFFFC0000 in the eflags comparison. Hardware deviations from the SDM (confirmed, NOT adopted by the core per AGENTS.md — the harness counts them as `skipped_hw_quirk`): a SIB with index=none and nonzero scale computes EA = base × scale + disp, verified numerically three times, while the suite's own `EA32` record follows the SDM formula and therefore disagrees with the registers (matching upstream's README note); and POPAD loads ESP's upper half from the stack image under a 16-bit stack — this one IS adopted, as "bits the stack width does not govern come from the image" is compatible with the SDM's discard and degenerates to the SDM on a 32-bit stack. Encoding notes (confirmed): Zydis reads 0x3E before indirect CALL/JMP as CET notrack, but on IA-32 it is a DS override, so the core's `SegmentOf` scans the raw prefixes itself; and no test in the suite distinguishes an indirect near call's push/read order (both orders pass `FF.2.MOO` 2420/2420 after the override fix), so the core keeps the SDM order.*
