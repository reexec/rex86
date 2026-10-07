# 가이드 : 소비자 게스트 명령 census / Guide : the consumer-guest instruction census

근거: [#5 설계](../design/20261007-i005-decoder-and-census.md) | 로그: [20261007-i005](../work-logs/20261007-i005-decoder-and-census.md)

`rex86_census`는 평탄 32비트 코드 이미지에서 명령 집합을 측정합니다. 하한(진입점 재귀 하강)과 상한(선형 스윕)을 함께 보고하며, 두 값의 간격이 정직하게 아는 것입니다. 측정 결과는 소비자 규칙에 따라 소비자와 측정 시점을 밝혀 `docs/analysis/`에 기록합니다.

*`rex86_census` measures the instruction set of a flat 32-bit code image, reporting a lower bound (recursive descent from the entries) and an upper bound (a linear sweep); the gap between them is what is honestly known. Record results in `docs/analysis/` naming the consumer and the measurement time, per the consumer rules.*

## 절차 / Procedure

1. 네이티브 호스트에서 빌드합니다(도구는 wasm32에서 빌드되지 않습니다).

   ```bash
   cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug
   # Windows: cmake -S . -B build -A Win32 && cmake --build build
   ```

2. 소비자에서 덤프를 만듭니다. 재배치와 복호화가 끝난 상태여야 합니다.
   * rePIU: 재배치된 LE 이미지(분석 도구가 쓰는 base `0x01000000`)와 LE 헤더의 진입점.
   * re2DJ: 복호화된 PE 섹션 덤프(`.text`)와 PE 헤더의 진입점, 확인된 추가 진입점(익스포트, 콜백).

3. 실행합니다. 주소 인자는 `0x` 접두사를 받습니다.

   ```bash
   rex86_census dump.bin --base 0x01000000 --entry 0x01000000 \
       [--entry ADDR]... [--exec START:LENGTH]...
   ```

   `--exec`를 생략하면 파일 전체를 실행 가능 구간으로 봅니다. 데이터 섹션이 섞인 덤프는 `--exec`로 코드 구간만 지정해야 상한이 부풀지 않습니다.

4. 보고를 읽습니다.
   * `reached_instructions`(하한)과 `sweep_decoded`(상한), `decode_stops`, `out_of_range_edges`.
   * `-- forms by volume --`의 누적 %가 인터프리터 구현 우선순위입니다(rePIU census의 "상위 17개 서명이 87.89%"와 같은 형식).
   * `x87_*` 줄은 80비트 x87 결정(#1 설계 결정 4)의 검증 수치입니다.

5. 결과를 `docs/analysis/`에 기록하고 그 디렉터리의 `README.md` 색인을 갱신합니다.

*Build on a native host (the tool is not built for wasm32). Produce the dump in the consumer with relocation and decryption already done: rePIU's relocated LE image at base `0x01000000` with the LE entry point, or re2DJ's decrypted `.text` dump with the PE entry point and any confirmed extra entries. Run the tool; without `--exec` the whole file counts as executable, so pass code ranges when data sections are mixed in, or the upper bound inflates. Read the report: the bounds and stops, the cumulative form coverage that orders the interpreter work (the same format as rePIU's "top 17 forms cover 87.89%"), and the `x87_*` lines that check design #1's 80-bit decision. Record the result in `docs/analysis/` and update that directory's `README.md` index.*
