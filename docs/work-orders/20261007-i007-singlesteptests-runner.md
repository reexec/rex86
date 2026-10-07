# #7 작업 지시 : SingleStepTests/80386 기반 독립 검증 러너 / #7 work order : the SingleStepTests/80386 independent validation runner

이슈: [#7](https://github.com/reexec/rex86/issues/7) | 설계: [20261007-i007](../design/20261007-i007-singlesteptests-runner.md) | 로그: [20261007-i007](../work-logs/20261007-i007-singlesteptests-runner.md)

## 작업 항목 / Tasks

1. `src/decode/decoder.h`: `Decoder::Mode { kLegacy32, kLegacy16 }` 생성자 인자(기본 `kLegacy32`)와 그에 맞는 Zydis 모드 선택.
2. `src/tools/sst/moo_reader.{h,cpp}`: MOO v1.1 파서. `MOO `/`META`/`TEST`(`NAME`, `BYTS`, `INIT`/`FINA`의 `RG32`/`RM32`/`RAM `, `EXCP`, `HASH`)를 읽고 모르는 청크는 길이로 건너뛴다.
3. `src/tools/sst/main.cpp`: `rex86_sst <dir-or-file>` 러너. 1차 디코더 검증(디코드 성공, 길이 일치), mnemonic 불일치는 정보성 보고, 실패 시 종료 코드 1. Emscripten 제외 빌드.
4. `tests/unit/moo_reader_test.cpp`: 합성 MOO 바이트열 파싱과 거부 경로. `tests/unit/decoder_test.cpp`: 16비트 모드 케이스 추가.
5. `docs/guides/singlesteptests.md`: 다운로드(gunzip, revocation 확인)와 실행 절차. `docs/kb/singlesteptests-moo.md`: 출처, 형식 요약, 링크. `docs/kb/README.md` 색인 갱신.
6. `ARCHITECTURE.md`: `src/tools/sst/` 행 추가. `README.md` 달성도 표에는 변화 없음(2단계 실행 검증까지 가야 목표 반영).
7. 실제 스위트 일부를 내려받아 `rex86_sst`로 실행하고 결과(BYTS에 HLT 포함 여부 포함)를 작업 로그에 기록.

*1 add `Decoder::Mode { kLegacy32, kLegacy16 }` defaulting to 32; 2 write the MOO v1.1 parser reading the header, META and TEST subchunks and skipping unknown chunks by length; 3 add the `rex86_sst` runner (stage-1 decoder validation: decode success and length equality, informational mnemonic report, exit 1 on failure, no Emscripten build); 4 unit-test the parser on synthesized MOO bytes plus 16-bit decoder cases; 5 write the download/run guide and the MOO knowledge-base topic with the kb index; 6 add the `src/tools/sst/` row to ARCHITECTURE.md; 7 download part of the real suite, run `rex86_sst`, and record the results (including whether BYTS holds the injected HLT) in the work log.*

## 완료 조건 / Completion criteria

* 로컬 Windows x86 빌드와 `ctest` 전체 통과.
* 내려받은 실제 MOO 파일들에서 `rex86_sst`의 디코드 실패 0, 길이 불일치 0 (불일치가 나오면 원인을 분석해 로그에 적고 수정 또는 업스트림 이슈로 처리).
* 공개 계약 무변경.

*The local Windows x86 build and full `ctest` pass; `rex86_sst` reports zero decode failures and zero length mismatches on the downloaded real MOO files (any mismatch is analyzed in the log and fixed or raised upstream); the public contract is unchanged.*
