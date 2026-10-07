# #5 작업 지시 : 디코더(Zydis) 도입과 독립 명령 census 도구 / #5 work order : the Zydis decoder and the standalone instruction census tool

이슈: [#5](https://github.com/reexec/rex86/issues/5) | 설계: [20261007-i005](../design/20261007-i005-decoder-and-census.md) | 로그: [20261007-i005](../work-logs/20261007-i005-decoder-and-census.md)

## 작업 항목 / Tasks

1. `third_party/zydis/`: rePIU가 검증한 Zydis v4.1.1 amalgamation(`Zydis.c`, `Zydis.h`, 라이선스 두 파일)을 두고 SHA-256이 rePIU `THIRD_PARTY_NOTICES.md`의 값과 같은지 확인한다. `THIRD_PARTY_NOTICES.md`에 기록한다.
2. `CMakeLists.txt`: 프로젝트 언어에 C 추가, `rex86_zydis` STATIC 타깃(`ZYDIS_STATIC_BUILD`, 경고 타깃 미적용), `rex86_core`에 `src/decode/decoder.cpp` 추가와 `rex86_zydis` PRIVATE 링크.
3. `src/decode/decoder.{h,cpp}`: `Decoder`(32비트 legacy 모드), `DecodedInstruction`(길이, mnemonic 이름, 피연산자 서명, x87 판별, 제어 흐름 분류와 직접 분기 목적지).
4. `src/tools/census/census.{h,cpp}`: 재귀 하강 도달, 선형 스윕, mnemonic/서명/x87/ISA 집계. `main.cpp`: 인자 해석과 파일 읽기, 보고 출력. `rex86_census` 타깃(Emscripten 제외).
5. `tests/unit/decoder_test.cpp`, `tests/unit/census_test.cpp`: 설계의 검증 절 항목.
6. `docs/guides/instruction-census.md`: 소비자 덤프에 census를 수행하는 절차. `ARCHITECTURE.md`의 decode/와 census 행을 **[구현됨]**으로 갱신.

*1 vendor the rePIU-proven Zydis v4.1.1 amalgamation under `third_party/zydis/` with SHA-256 checked against rePIU's notice and record it in `THIRD_PARTY_NOTICES.md`; 2 add C to the project languages, the `rex86_zydis` STATIC target without the warnings target, and `src/decode/decoder.cpp` to `rex86_core` with a PRIVATE link; 3 implement `Decoder` (32-bit legacy mode) and `DecodedInstruction` (length, mnemonic name, operand signature, x87, control-flow class and direct target); 4 implement the census logic in `census.{h,cpp}` with `main.cpp` doing arguments, file reading and the report, as the `rex86_census` target excluded under Emscripten; 5 add the decoder and census unit tests from the design's verification section; 6 write `docs/guides/instruction-census.md` and mark decode/ and census **[implemented]** in `ARCHITECTURE.md`.*

## 완료 조건 / Completion criteria

* 로컬 Windows x86(MSVC `-A Win32`) 빌드와 `ctest` 전체 통과, `rex86_census`가 합성 바이너리에서 설계의 보고 형식을 출력.
* 공개 계약(`include/rex86/`) 무변경. Zydis 타입이 공개 헤더에 나타나지 않음.
* 나머지 네 호스트는 push 시 CI로 확인하고 결과를 작업 로그에 적는다.

*The local Windows x86 (MSVC `-A Win32`) build and the full `ctest` run pass and `rex86_census` prints the design's report format on a synthetic binary; the public contract is unchanged and no Zydis type appears in a public header; the other four hosts are checked by CI on push and the results recorded in the work log.*
