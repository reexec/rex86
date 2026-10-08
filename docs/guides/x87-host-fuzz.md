# 가이드 : x87 호스트 CPU 대조 fuzz / Guide : the x87 host-CPU comparison fuzz

근거: [#19 설계](../design/20261008-i019-x87-increment-1.md) 결정 6 | 로그: [20261008-i019](../work-logs/20261008-i019-x87-increment-1.md) | 실측 기록: [x87 호스트 대조](../analysis/x87-host-comparison.md)

`rex86_x87_fuzz`는 무작위 x87 명령 하나를 호스트 CPU와 코어에서 같은 입력으로 실행하고 결과를 비교합니다. x86 또는 x86-64 Linux에서만 빌드됩니다. CTest는 짧은 고정 시드 실행(`20000 1`)을 등록합니다.

*`rex86_x87_fuzz` runs one random x87 instruction on the host CPU and on the core from the same input and compares the results; it builds on x86 or x86-64 Linux only, and CTest registers a short fixed-seed run (`20000 1`).*

## 절차 / Procedure

1. 빌드합니다.

   ```bash
   cmake --preset linux-x64-release && cmake --build --preset linux-x64-release
   ```

2. 긴 실행: 회수와 시드를 줍니다. 마지막 줄의 `mismatches`가 0이어야 합니다. `vendor_deviations`는 분석 문서 3절의 제조사 이탈(마스크 안 된 #IA 비교, AMD에서 그대로 돌려주는 denormal의 #U)을 따로 센 수입니다.

   ```bash
   for seed in $(seq 100 119); do ./build/linux-x64-release/bin/rex86_x87_fuzz 2000000 $seed | tail -1; done
   ```

3. 불일치가 나면 `MISMATCH` 줄 아래의 `--replay` 인자로 그 한 건을 재현합니다. 입력, 호스트, 코어의 CW/SW/TW, EFLAGS, EAX, 레지스터, 메모리를 나란히 출력합니다.

   ```bash
   ./build/linux-x64-release/bin/rex86_x87_fuzz --replay <code> <image> <data> <flags>
   ```

4. 새로 확인한 동작은 `docs/analysis/x87-host-comparison.md`에 확인됨/추정/미확정과 측정 호스트(제조사, 모델)를 밝혀 기록합니다. 지금 기록은 AMD Zen 3(#19)과 Intel Cascade Lake(#22)입니다. 다른 세대, 특히 P6 세대 실물의 결과는 가치가 큽니다.

5. `--record <파일>`은 일치한 사례를 trace로 씁니다(제조사 이탈 제외). 저장소의 `tests/traces/x87.rxt`가 이렇게 만들어졌고 모든 호스트의 ctest가 재생합니다([정수 fuzz와 trace 가이드](integer-host-fuzz-and-traces.md)).

*Build, run long seeds (the last line's `mismatches` must be 0; `vendor_deviations` counts the vendor deviations of the analysis topic's section 3 apart), reproduce any mismatch with the `--replay` arguments printed under its `MISMATCH` line (which dumps input, host and core side by side) and record new facts in the analysis topic with their status and the measuring host's vendor and model; today's record covers an AMD Zen 3 (#19) and an Intel Cascade Lake (#22), and other generations, P6 hardware above all, are valuable. `--record <file>` writes the matching cases as traces (vendor deviations left out); that is how `tests/traces/x87.rxt` was made, replayed by every host's ctest ([the integer fuzz and traces guide](integer-host-fuzz-and-traces.md)).*
