# 가이드 : 견고성 fuzz / Guide : the robustness fuzz

근거: [#31 설계](../design/20261009-i031-robustness-fuzz.md) | 로그: [20261009-i031](../work-logs/20261009-i031-robustness-fuzz.md) | 기록: [견고성 fuzz 결과](../analysis/robustness-fuzz.md)

릴리스 규모의 실행은 tag push 때 GitHub Actions가 맡습니다([캠페인 가이드](fuzz-campaign.md)).

*Release-scale runs happen on GitHub Actions at each tag push ([campaign guide](fuzz-campaign.md)).*

`rex86_robust`는 임의 메모리(보호 구역으로 둘러쌈), 임의 페이지 속성, 임의 CPU 상태와 기능, 임의로 답하는 호스트로 코어를 돌리고, 어떤 입력에서도 지켜져야 하는 불변식 I1~I6을 확인합니다(설계 결정 1). 의미가 맞는지는 묻지 않습니다. 그것은 호스트 대조 fuzz들의 일입니다. 모든 호스트에서 빌드되며, CTest는 `--cases 300`을 등록합니다.

*`rex86_robust` runs the core on arbitrary memory (between guard zones), page attributes, CPU state and features with a randomly answering host, and checks invariants I1-I6 that must hold for any input (design decision 1); whether results are right is the host-comparison fuzzes' question. It builds on every host and CTest registers `--cases 300`.*

## 절차 / Procedure

1. **새니타이저로 빌드합니다.** 크래시와 경계 밖 읽기(I1, I2)는 ASan/UBSan 빌드에서 가장 잘 잡힙니다. 하네스는 ASan 빌드에서 보호 구역을 poison해 읽기까지 잡습니다.

   ```bash
   cmake -S . -B build/linux-x64-sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug \
       -DREX86_BUILD_TESTS=ON -DREX86_SANITIZE=address,undefined
   cmake --build build/linux-x64-sanitize
   ./build/linux-x64-sanitize/bin/rex86_robust --cases 50000 --seed 2000000
   ```

2. **긴 실행은 Release로 시드 범위를 나눠 돌립니다.** 마지막 줄이 `result=ok`, `violations=0`이어야 합니다. `stops:`와 `faults:` 줄은 무엇이 얼마나 일어났는지(폴트는 `FaultKind` 번호)를 보여 주며, 생성기가 어느 경로에 닿는지 가늠하는 데 씁니다.

   ```bash
   ./build/linux-x64-release/bin/rex86_robust --cases 500000 --seed 1000000
   ```

3. **위반이 나면** `VIOLATION seed=<S>` 줄의 시드로 그 케이스만 다시 돌립니다. `--case`는 Run마다 이벤트와 EIP를 출력합니다.

   ```bash
   ./build/linux-x64-release/bin/rex86_robust --case <S>
   ```

   고친 뒤에는 그 입력을 단위 테스트로 고정합니다(설계 결정 5).

4. **libFuzzer**(Clang): 입력의 앞 16바이트가 생성기를, 나머지가 EIP의 코드 바이트를 정하므로 변형이 명령에 직접 닿습니다. 게스트 메모리는 64 KiB 이하로 묶입니다.

   ```bash
   CC=clang CXX=clang++ cmake -S . -B build/linux-x64-libfuzzer -G Ninja \
       -DCMAKE_BUILD_TYPE=RelWithDebInfo -DREX86_BUILD_TESTS=ON -DREX86_LIBFUZZER=ON \
       -DREX86_SANITIZE=address,undefined
   cmake --build build/linux-x64-libfuzzer --target rex86_robust_libfuzzer
   cd build/linux-x64-libfuzzer && mkdir -p corpus crashes
   ./bin/rex86_robust_libfuzzer corpus -max_total_time=1800 -timeout=10 -max_len=512 \
       -len_control=0 -artifact_prefix=crashes/
   ```

   크래시는 `crashes/`에 입력으로 남고, `./bin/rex86_robust_libfuzzer crashes/<파일>`이 재현합니다. CI의 `linux-x64-libfuzzer` 작업은 60초 동안 돌고 크래시 입력을 artifact로 올립니다.

5. **기록합니다.** 실행 규모, 호스트, 찾은 결함을 분석 문서에 적습니다.

*Build with the sanitizers (I1 and I2 are caught best there; the harness poisons the guard zones under ASan so reads are caught too); run long Release campaigns over seed ranges, expecting `result=ok` and `violations=0` (the `stops:` and `faults:` lines show what happened, faults by `FaultKind` number); rerun a `VIOLATION seed=<S>` with `--case <S>`, which prints every Run's event and EIP, and pin the fixed input as a unit test (decision 5); with Clang, run libFuzzer, whose first 16 input bytes drive the generator and the rest become the code at EIP, with guest memory capped at 64 KiB, crashes landing in `crashes/` and replayed by passing the file (CI's `linux-x64-libfuzzer` runs it for 60 seconds and uploads crashing inputs); and record scale, host and findings in the analysis topic.*

## 하네스가 보지 않는 것 / What it does not check

* 결과의 옳고 그름. 호스트 CPU 대조 fuzz와 SingleStepTests의 일입니다.
* 한 명령의 실행 시간. REP 문자열은 한 Step 안에서 끝까지 돌므로, 큰 메모리에서는 오래 걸릴 수 있습니다([#32](https://github.com/reexec/rex86/issues/32)). 하네스는 메모리를 1 MiB 이하로 두어 이것을 묶습니다.
* 번역 백엔드. 3단계에서 같은 하네스가 백엔드를 인터프리터와 비교합니다(설계 결정 6).

*It does not check whether results are right (the host-comparison fuzzes and SingleStepTests do), how long one instruction takes (a REP string runs to completion within one Step and can take long over large memory, #32; the harness bounds memory at 1 MiB), or translation backends (phase 3 extends the same harness, design decision 6).*
