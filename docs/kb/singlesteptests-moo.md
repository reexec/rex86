# SingleStepTests와 MOO 형식 / SingleStepTests and the MOO format

## SingleStepTests

[SingleStepTests](https://github.com/SingleStepTests)는 실제 CPU에서 생성한 명령 단위 테스트 모음이다. [80386 스위트](https://github.com/SingleStepTests/80386)(MIT)는 Intel 386EX의 버스를 Arduino로 조작해 만들었고, opcode·피연산자 조합당 100~2,500개 테스트(real mode 941 파일)가 있다. 각 테스트는 초기/최종 레지스터와 RAM, undefined 상태 마스크, 예외 기록, SHA-1 해시를 담는다. 테스트 생성은 두 번 반복해 일치를 요구하고, 생성 후 재생 검증을 거친다. 잘못으로 판명된 테스트는 `revocation_list.txt`에 해시로 철회된다.

핵심 성질: 하드웨어 유래 기준이므로 **x86이 아닌 호스트에서도** 대조 기준이 된다(이 저장소의 호스트 CPU 대조 fuzz는 x86 러너에서만 돈다). 한계: real mode 중심(protected mode 업스트림 미완), FPU 미포함, 386EX의 16비트 버스라 사이클 데이터는 386DX와 다름(이 저장소는 사이클을 쓰지 않음).

*Hardware-generated per-instruction tests; the 80386 suite (MIT) was captured from a real 386EX, 100–2,500 tests per opcode/operand combination across 941 real-mode files, each holding initial/final registers and RAM, undefined-state masks, exception records and a SHA-1 hash, generated twice and replay-verified, with bad tests revoked by hash. Being hardware-derived, they serve as a comparison reference on non-x86 hosts too; limits are the real-mode focus (protected mode pending upstream), no FPU, and 386EX 16-bit-bus cycle timings this repository does not use.*

## MOO v1.1

[사양](https://github.com/dbalsom/moo/blob/main/doc/moo_format_v1.md)이 완결적인 chunked little-endian 바이너리 형식. 모든 청크는 `ASCII_ID(4) + 길이(u32) + payload`이고, 파서는 모르는 청크를 길이로 건너뛴다.

* 구조: `MOO `(버전, 테스트 수, CPU ID) → `META`(opcode, mnemonic, cpu_mode) → `TEST`*.
* `TEST` 안: `NAME`, `BYTS`(명령 바이트 + 생성기가 주입한 HLT 1바이트, #7 실측), `INIT`/`FINA`(`RG32` 레지스터 20개 비트마스크 + 값, `RM32` 마스크, `RAM `(주소 u32 + 값 u8)), `CYCL`(버스 사이클, 미사용), `EXCP`(번호 u8 + 플래그 주소 u32), `HASH`(SHA-1 20바이트).
* `RM32` 마스크의 극성(#13 실측): **비트 1 = 정의된 비트(비교 대상)**, 비트 0 = 미정의(무시). SHL의 eflags 마스크 `0xFFFFFFEF`(AF만 미정의), MUL `0xFFFFFF2B`(SF/ZF/AF/PF 미정의), DIV `0xFFFFF72A`(산술 플래그 전부 미정의)가 SDM의 미정의 목록과 정확히 일치한다. 마스크가 없는 레지스터는 전부 정의로 본다. 테스트 수준 마스크가 파일 수준보다 우선.
* `RG32` 레지스터 순서(bit 0부터): cr0, cr3, eax, ebx, ecx, edx, esi, edi, ebp, esp, cs, ds, es, fs, gs, ss, eip, eflags, dr6, dr7.
* 이 저장소의 파서는 `src/tools/sst/moo_reader.{h,cpp}`로 사양 기반 자체 구현이다.

*A chunked little-endian binary with a complete specification: every chunk is a 4-byte ASCII id, a u32 length and the payload, with unknown chunks skipped by length. `MOO ` (version, test count, CPU id) and `META` (opcode, mnemonic, cpu mode) precede the `TEST` chunks, each holding `NAME`, `BYTS` (instruction bytes plus one generator-injected HLT, confirmed in #7), `INIT`/`FINA` with the 20-register `RG32` bitmask set, `RM32` masks and `RAM ` entries, the unused `CYCL`, `EXCP` (u8 number, u32 flag address) and the 20-byte SHA-1 `HASH`. The `RM32` polarity, measured in #13: **a set bit marks a DEFINED bit to compare** and a clear bit an undefined one to ignore — SHL's eflags mask `0xFFFFFFEF` (only AF undefined), MUL's `0xFFFFFF2B` and DIV's `0xFFFFF72A` match the SDM's undefined lists exactly; a register without a mask is fully defined, and a test-level mask wins over the file-level one. This repository's parser, `src/tools/sst/moo_reader.{h,cpp}`, is written from the specification.*
