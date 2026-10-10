# 가이드 : 정수 명령 호스트 CPU 대조 fuzz와 trace / Guide : the integer host-CPU comparison fuzz and traces

근거: [#22 설계](../design/20261008-i022-integer-host-fuzz-and-trace.md) | 로그: [20261008-i022](../work-logs/20261008-i022-integer-host-fuzz-and-trace.md) | 실측 기록: [정수 호스트 대조](../analysis/integer-host-comparison.md) | 기법: [트랩 플래그 단일 스텝](../kb/trap-flag-single-step.md)

릴리스 규모의 실행은 tag push 때 GitHub Actions가 맡습니다([캠페인 가이드](fuzz-campaign.md)).

*Release-scale runs happen on GitHub Actions at each tag push ([campaign guide](fuzz-campaign.md)).*

`rex86_int_fuzz`는 무작위 32비트 정수 명령 하나를 호스트 CPU와 코어에서 같은 주소, 같은 입력으로 실행해 비교합니다. **i386 Linux 프로세스에서만** 빌드되고 돕니다. `rex86_trace`는 기록된 trace를 코어로 재생하며 모든 호스트에서 빌드됩니다. CTest는 짧은 정수 fuzz(`50000 1`, i386만)와 `tests/traces/*.rxt` 재생(`rex86_trace_corpus`, 모든 호스트)을 등록합니다.

*`rex86_int_fuzz` runs one random 32-bit integer instruction on the host CPU and on the core at the same addresses from the same input and compares them; it builds and runs **in i386 Linux processes only**. `rex86_trace` replays recorded traces on the core and builds on every host. CTest registers a short integer fuzz (`50000 1`, i386 only) and the replay of `tests/traces/*.rxt` (`rex86_trace_corpus`, every host).*

## 절차 / Procedure

1. i386 빌드. 64비트 개발 환경이면 multilib(`g++-multilib`)로 프리셋을 씁니다. 긴 실행은 Release가 빠릅니다.

   ```bash
   cmake --preset linux-x86-debug && cmake --build --preset linux-x86-debug
   # Release
   cmake -S . -B build/linux-x86-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
     -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32 -DCMAKE_EXE_LINKER_FLAGS=-m32
   cmake --build build/linux-x86-release
   ```

2. 긴 실행: 회수와 시드를 줍니다. 마지막 줄의 `mismatches`가 0이어야 합니다. `vendor_deviations`는 분석 문서의 제조사 이탈(Zen 5의 CMPS 폴트 주소, Zen 3의 BOUND 읽기 순서)을 따로 센 수이고, 그 사례는 `--record`에 들어가지 않습니다. `--stats`는 mnemonic별 retire와 폴트 수를, `--only <mnemonic>`은 한 형태만 돌립니다.

   ```bash
   for seed in $(seq 100 109); do ./build/linux-x86-release/bin/rex86_int_fuzz 1000000 $seed | head -1; done
   ```

3. 불일치는 `--failures <파일>`로 trace가 되어 어느 호스트에서든 재현됩니다. `MISMATCH` 줄에 그 파일 안의 사례 번호가 붙습니다.

   ```bash
   ./build/linux-x86-release/bin/rex86_int_fuzz 200000 7 --failures /tmp/f.rxt
   ./build/linux-x86-release/bin/rex86_trace --dump 0 /tmp/f.rxt   # 입력, 기대값, 메모리 변경, 재생 결과
   ```

4. 새로 확인한 동작은 `docs/analysis/integer-host-comparison.md`에 확인됨/추정/미확정과 측정 호스트(제조사, family/model/stepping)를 밝혀 기록합니다. SDM이 분명하면 코어를 SDM에 맞추고, 침묵하면 실측값을 쓰되 SingleStepTests(386EX)와 충돌하지 않는지 `rex86_sst --execute`로 확인합니다. 세대마다 다른 동작은 마스크하고 분석 문서에 적습니다.

*Build for i386 (with multilib on a 64-bit machine; Release is faster for long runs), run long seeds (the last line's `mismatches` must be 0, `vendor_deviations` counting the analysis topic's vendor deviations, Zen 5's CMPS fault address and Zen 3's BOUND read order, apart and out of `--record`; `--stats` prints retired and faulted counts per mnemonic, `--only <mnemonic>` runs one form), turn mismatches into traces with `--failures <file>` (each `MISMATCH` line names its case number) and spell one out with `rex86_trace --dump`, then record new facts in the analysis topic with their status and the host's vendor and family/model/stepping. Fix the core toward the SDM where it is clear; where it is silent, adopt the measurement after checking `rex86_sst --execute` still agrees with the 386EX; mask what differs between generations and say so in the analysis.*

## trace 묶음 다시 만들기 / Regenerating the corpus

`tests/traces/`의 묶음은 고정 시드로 만든 기대값입니다. 생성기나 비교 규칙이 바뀌어 다시 만들 때만 갱신하고, 바꾼 이유를 작업 로그에 적습니다. 크기는 합쳐서 수 MB 이하로 둡니다.

```bash
./build/linux-x86-release/bin/rex86_int_fuzz 12000 2201 --record tests/traces/int32.rxt
./build/linux-x64-release/bin/rex86_x87_fuzz 2600 2202 --record tests/traces/x87.rxt
./build/linux-x64-release/bin/rex86_trace tests/traces/*.rxt
```

x87 fuzz는 기록하는 사례마다 그 자리에서 재생해 기록 자체를 검사하고, 제조사 이탈(`vendor_deviations`) 사례는 기록하지 않습니다.

*The corpus holds fixed-seed expectations: regenerate it only when the generator or the comparison rules change, with the reason in the work log, and keep it to a few MB. The x87 fuzz replays each case it records on the spot and leaves vendor deviations out.*

## trace 형식 / The trace format

리틀 엔디안 바이너리 `"RX86TRC1" | u32 version | u32 count | case...`이고 정의는 `src/trace/trace_format.h`에 있습니다. 영역의 초기 내용은 `fill_seed`의 xorshift32 수열이고, 사례는 그와 다른 바이트(입력 patch, 기대 diff)만 담습니다. 소비자나 다른 도구가 trace를 만들 때도 이 형식과 `trace::Replay`를 그대로 씁니다.

*Little-endian binary, `"RX86TRC1" | u32 version | u32 count | case...`, defined in `src/trace/trace_format.h`. Regions start as an xorshift32 sequence from `fill_seed`, and a case carries only the bytes that differ from it (input patches, expected diffs). Consumers and other tools that produce traces use the same format and `trace::Replay`.*
