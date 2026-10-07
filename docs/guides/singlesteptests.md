# 가이드 : SingleStepTests/80386 검증 / Guide : SingleStepTests/80386 validation

근거: [#7 설계](../design/20261007-i007-singlesteptests-runner.md) | 로그: [20261007-i007](../work-logs/20261007-i007-singlesteptests-runner.md)

[SingleStepTests/80386](https://github.com/SingleStepTests/80386)(MIT)은 실제 Intel 386EX에서 생성된 명령 단위 테스트입니다. `rex86_sst`가 이 스위트로 코어를 검증합니다. 현재는 1차(디코더: 디코드 성공과 길이 일치)이고, 인터프리터가 생기면 같은 러너가 상태 실행 비교로 확장됩니다. 테스트 데이터는 저장소에 넣지 않습니다.

*SingleStepTests/80386 (MIT) are per-instruction tests generated on a real Intel 386EX; `rex86_sst` validates the core against them — currently stage 1 (decoder: decode success and length equality), growing into execute-and-compare once the interpreter exists. The data never enters this repository.*

## 절차 / Procedure

1. 스위트를 받고 압축을 풉니다(전체 약 1.4 GB 압축 해제 기준, 941개 파일).

   ```bash
   git clone --depth 1 https://github.com/SingleStepTests/80386.git
   gunzip 80386/v1_ex_real_mode/*.MOO.gz
   ```

2. `revocation_list.txt`를 확인합니다. 철회된 해시의 테스트는 업스트림이 잘못으로 인정한 것이므로 불일치가 그 테스트에서만 나면 무시합니다.

3. 러너를 실행합니다(디렉터리 또는 단일 파일, `--verbose`로 실패 상세).

   ```bash
   ./bin/rex86_sst 80386/v1_ex_real_mode
   ```

   요약 줄의 `decode_failures`와 `length_mismatches`가 0이어야 하고, `expected_ud`는 하드웨어가 #UD(예외 6)를 기록한 인코딩(`lock ret` 같은 비잠금 가능 명령의 LOCK)에 대한 디코드 거부로 정상입니다.

4. CTest로 반복 실행하려면 configure에 `-DREX86_SST_DIR=<경로>`를 주면 `rex86_sst` 테스트가 등록됩니다.

참고: 스위트의 완성 구간은 real mode(기본 16비트 피연산자)라 러너는 16비트 디코드 모드를 씁니다. BYTS는 테스트 대상 명령 뒤에 생성기가 경계 검출용으로 주입한 HLT(0xF4) 한 바이트를 포함합니다(#7 작업에서 실측 확인).

*Clone and gunzip the suite (941 files), check `revocation_list.txt` (mismatches confined to revoked hashes are ignorable), and run `rex86_sst` on the directory; `decode_failures` and `length_mismatches` must be 0, while `expected_ud` counts decode refusals matching the hardware's recorded #UD (exception 6), such as LOCK on a non-lockable instruction. Passing `-DREX86_SST_DIR=<path>` at configure time registers the ctest entry. The suite's complete section is real mode (16-bit default operand size), so the runner decodes in 16-bit mode, and BYTS carries one injected HLT (0xF4) after the tested instruction for boundary detection, confirmed empirically in task #7.*
