# #7 작업 로그 : SingleStepTests/80386 기반 독립 검증 러너 / #7 work log : the SingleStepTests/80386 independent validation runner

이슈: [#7](https://github.com/reexec/rex86/issues/7) | 설계: [20261007-i007](../design/20261007-i007-singlesteptests-runner.md) | 지시서: [20261007-i007](../work-orders/20261007-i007-singlesteptests-runner.md)

## 2026-10-07

- **출발점**: 사용자가 rePIU와 re2DJ에 의존하지 않는 독립 공개 샘플 검증을 요구했다. 조사(설계의 후보 표)에서 SingleStepTests/80386(MIT)을 채택하고, test386.asm(GPLv3)은 공식 경로에서 제외, DOOM 1.9 shareware와 CoreMark는 후속 후보로 기록했다.
- **구현**
  - `Decoder::Mode { kLegacy32, kLegacy16 }`(기본 32). real mode 스위트는 16비트 기본 피연산자 크기라서 필요하고, `Features::segments_16bit`의 디코드 쪽 선행 작업이기도 하다.
  - `src/tools/sst/moo_reader.{h,cpp}`: MOO v1.1 사양 기반 자체 파서. `MOO `/`META`/`TEST`(`NAME`, `BYTS`, `INIT`/`FINA`의 `RG32`/`RM32`/`RAM `, `EXCP`)를 읽고 `CYCL`/`QUEU`/미지 청크는 길이로 건너뛴다.
  - `src/tools/sst/main.cpp`: `rex86_sst <dir-or-file> [--verbose]`. 1차 디코더 검증. `REX86_SST_DIR` configure 변수로 ctest 등록.
- **실측으로 확인한 두 가지** (첫 실행에서 거의 전 테스트가 불일치로 나와 원인 분석)
  1. **BYTS는 명령 + 주입된 HLT다.** `EB.MOO`의 첫 테스트 바이트가 `EB 87 F4`로, 생성기가 경계 검출용으로 붙인 HLT(0xF4) 1바이트가 항상 뒤따른다(README의 "two instructions: the instruction being tested, followed by a HALT"와 일치). 러너는 마지막 바이트가 0xF4면 기대 길이를 `size-1`로 잡는다. 명령 자신의 마지막 바이트가 0xF4인 경우에도 산술이 같아 손실이 없다.
  2. **디코드 거부가 하드웨어 #UD와 일치하는 집합이 있다.** `lock ret`, `lock add dh,bh` 같은 비잠금 가능 명령의 LOCK은 실제 386도 #UD(예외 6)를 올리고 스위트가 `EXCP` 6으로 기록한다. Zydis의 디코드 거부는 불일치가 아니라 일치이므로 `expected_ud`로 따로 센다.
- **검증**
  - 단위 테스트: 합성 MOO 바이트열 파서 테스트와 16비트 디코더 케이스 추가. `rex86_unit_tests` checks 227 failures 0.
  - 로컬 Windows x86(MSVC `-A Win32`, 경고를 오류로) 빌드 깨끗, `ctest` 2/2 통과.
  - **전체 스위트 실행**: 941개 파일 전부(real mode, `--depth 1` 클론 후 gunzip), 1,758,700개 테스트에서 `parse_failures=0 decode_failures=0 length_mismatches=0 expected_ud=34864`. 약 1분 57초(Debug 빌드). `mnemonic_mismatches=83802`는 Zydis와 스위트의 이름 체계 차이(jz/je 등)로 정보성이다.
  - 확인하지 못한 것: 나머지 네 호스트는 push 시 CI.
- **문서**: 가이드 `docs/guides/singlesteptests.md`, 지식 기반 `docs/kb/singlesteptests-moo.md`(+ kb 색인), `ARCHITECTURE.md`에 `src/tools/sst/`와 `rex86_sst` 행, README 달성도 표의 1단계 상태에 #7 반영.
- **남은 것**: 2차(인터프리터 실행 비교)는 인터프리터 작업에서 같은 러너를 확장한다. CI에서의 스위트 실행은 데이터 캐시 전략과 함께 그때 판단한다.

*Starting point: the user required validation independent of rePIU and re2DJ; SingleStepTests/80386 (MIT) was adopted, test386.asm (GPLv3) excluded from the official path, and DOOM 1.9 shareware and CoreMark recorded as follow-up candidates. Implementation: `Decoder::Mode { kLegacy32, kLegacy16 }` (the real-mode suite's default operand size is 16 bits, and this is the decode-side groundwork for `Features::segments_16bit`); a specification-based MOO v1.1 parser reading the header, META and TEST subchunks and skipping CYCL/QUEU/unknown chunks by length; and the `rex86_sst` runner with the `REX86_SST_DIR` ctest hook. Two facts confirmed empirically after the first run mismatched almost everywhere: BYTS holds the tested instruction plus one generator-injected HLT (0xF4) for boundary detection, so the expected length is `size-1` when the last byte is 0xF4 (an instruction whose own last byte is 0xF4 yields the same arithmetic); and decode refusals for LOCK on non-lockable instructions agree with the hardware's recorded #UD (exception 6), counted separately as `expected_ud`. Verification: parser unit tests on synthesized MOO bytes plus 16-bit decoder cases (227 checks, 0 failures); a clean local Windows x86 build passing ctest 2/2; and the full suite — all 941 real-mode files, 1,758,700 tests — with `parse_failures=0 decode_failures=0 length_mismatches=0 expected_ud=34864` in about 1 m 57 s (Debug), the 83,802 mnemonic mismatches being informational naming differences (jz/je and the like); the other four hosts are checked by CI on push. Documents: the guide, the knowledge-base topic with its index, the ARCHITECTURE rows and the README attainment cell. Remaining: stage 2 (interpreter execute-and-compare) extends the same runner in the interpreter task, where CI adoption is decided together with a data caching strategy.*
