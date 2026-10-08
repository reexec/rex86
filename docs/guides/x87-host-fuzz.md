# 가이드 : x87 호스트 CPU 대조 fuzz / Guide : the x87 host-CPU comparison fuzz

근거: [#19 설계](../design/20261008-i019-x87-increment-1.md) 결정 6, [#25 설계](../design/20261008-i025-x87-transcendentals.md) 결정 5 | 로그: [20261008-i019](../work-logs/20261008-i019-x87-increment-1.md), [20261008-i025](../work-logs/20261008-i025-x87-transcendentals.md) | 실측 기록: [x87 호스트 대조](../analysis/x87-host-comparison.md)

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

## 초월함수 / The transcendentals

FSIN, FCOS, FSINCOS, FPTAN, FPATAN, F2XM1, FYL2X, FYL2XP1(#25)은 비트 일치 대신 허용치로 판정합니다. 결과 레지스터가 RC = nearest에서 1 ulp, 다른 방향에서 2 ulp 이내이거나 C1만 다르면, 나머지가 모두 같을 때 `within_tolerance`로 셉니다. 코어는 정확히 반올림하고, 호스트는 SDM 한계(1, 1.5 ulp) 안에 있기 때문입니다. `tolerance:` 줄이 그 구성(C1만 다름, 1 ulp, 2 ulp)을 보여 줍니다. 허용 사례는 trace로 기록하지 않습니다.

1. 초월함수만 돌립니다. `--only`는 쉼표로 여러 mnemonic을 받습니다. `mismatches`가 0이어야 합니다.

   ```bash
   all=fsin,fcos,fsincos,fptan,fpatan,f2xm1,fyl2x,fyl2xp1
   for seed in $(seq 300 309); do ./build/linux-x64-release/bin/rex86_x87_fuzz 2000000 $seed --only $all | grep -E '^(forms|tolerance)'; done
   ```

2. `--show-tolerance`는 허용 사례마다 `TOLERANCE` 줄과 `--replay` 인자를 출력합니다.

3. 코어가 정확히 반올림하는지는 오라클로 따로 확인합니다. `--dump <파일>`이 초월함수 사례(입력과 양쪽 결과)를 쓰고, `scripts/x87_transcendental_oracle.py`가 SDM 모델값(66비트 Pi 축소와 참 함수)을 Python `decimal` 170자리로 계산해 RC 방향으로 반올림한 뒤 비교합니다. 계산 경로의 사례만 봅니다(특수값, 범위 밖, tiny 지름길, 정의역 밖 pass-through, 마스크 안 된 #U/#O 제외). `core_equal`과 `core_c1_equal`이 `cases`와 같아야 합니다. `host_equal`은 호스트가 정확히 반올림한 비율입니다. Python 3만 있으면 되고 외부 패키지는 쓰지 않습니다. 8만 건에 수 분이 걸립니다.

   ```bash
   ./build/linux-x64-release/bin/rex86_x87_fuzz 80000 500 --only $all --dump /tmp/dump.txt > /dev/null
   python3 scripts/x87_transcendental_oracle.py /tmp/dump.txt --show 10
   ```

4. 초월함수 trace 묶음 `tests/traces/x87_transcendental.rxt`는 다음으로 다시 만듭니다(일치 사례만 기록).

   ```bash
   ./build/linux-x64-release/bin/rex86_x87_fuzz 2000 2503 --only $all --record tests/traces/x87_transcendental.rxt
   ```

*The transcendentals (#25) are judged with a tolerance instead of bit equality: result registers within 1 ulp (RC nearest) or 2 ulps (otherwise), or C1 alone differing, count as `within_tolerance` when everything else agrees, since the core rounds correctly and the host stays within the SDM's 1 and 1.5 ulps; the `tolerance:` line breaks them down (C1 only, 1 ulp, 2 ulps), and tolerated cases are not recorded as traces. Run the eight alone with a comma-separated `--only` (mismatches must be 0); `--show-tolerance` prints each tolerated case with its `--replay` arguments. Correct rounding is checked apart: `--dump <file>` writes the transcendental cases and `scripts/x87_transcendental_oracle.py` evaluates the SDM model (66-bit Pi reduction, true functions) with Python's `decimal` at 170 digits, rounds it in RC's direction and compares, on computed paths only (special values, out-of-range arguments, the tiny shortcut, out-of-domain pass-through and unmasked #U/#O left out); `core_equal` and `core_c1_equal` must equal `cases`, while `host_equal` is the share the host rounds correctly. It needs Python 3 alone, no packages, and takes a few minutes for 80,000 cases. The transcendental corpus `tests/traces/x87_transcendental.rxt` is regenerated with the `--record` command above (matching cases only).*
