# 가이드 : SIMD 호스트 CPU 대조 fuzz / Guide : the SIMD host-CPU comparison fuzz

근거: [#29 설계](../design/20261009-i029-mmx-and-sse.md) 결정 7 | 로그: [20261009-i029](../work-logs/20261009-i029-mmx-and-sse.md) | 실측 기록: [SIMD 호스트 대조](../analysis/simd-host-comparison.md)

릴리스 규모의 실행은 tag push 때 GitHub Actions가 맡습니다([캠페인 가이드](fuzz-campaign.md)).

*Release-scale runs happen on GitHub Actions at each tag push ([campaign guide](fuzz-campaign.md)).*

`rex86_simd_fuzz`는 무작위 MMX 또는 SSE 명령 하나를 FXRSTOR와 FXSAVE 사이에서 호스트 CPU와 코어로 실행하고 결과를 비교합니다. x86 또는 x86-64 Linux에서만 빌드됩니다. CTest는 짧은 고정 시드 실행(`20000 1`)을 등록합니다.

*`rex86_simd_fuzz` runs one random MMX or SSE instruction between FXRSTOR and FXSAVE on the host CPU and on the core and compares the results; it builds on x86 or x86-64 Linux only, and CTest registers a short fixed-seed run (`20000 1`).*

## 절차 / Procedure

1. 빌드합니다. 긴 실행은 Release가 빠릅니다. i386 프로세스로도 돌려 봅니다(64비트 기계는 multilib 필요).

   ```bash
   cmake --preset linux-x64-release && cmake --build --preset linux-x64-release
   cmake --preset linux-x86-debug && cmake --build --preset linux-x86-debug
   ```

2. 긴 실행: 회수와 시드를 줍니다. 첫 줄의 `mismatches`가 0이어야 합니다. `within_tolerance`는 RCPPS/RSQRTPS와 스칼라형의 근사값 차이를 따로 센 수입니다(SDM 한계 안). `faults:` 줄은 폴트 종류(`FaultKind` 번호)별 일치 건수입니다.

   ```bash
   for seed in $(seq 300 309); do ./build/linux-x64-release/bin/rex86_simd_fuzz 1000000 $seed | head -2; done
   ```

3. 특정 명령만: `--only`는 쉼표 목록을 받습니다.

   ```bash
   ./build/linux-x64-release/bin/rex86_simd_fuzz 200000 7 --only cvtpi2ps,cvtps2pi
   ```

4. 불일치는 `MISMATCH` 줄 아래에 명령 바이트, 메모리, EAX, EFLAGS, 입력 FXSAVE 이미지(0~287)가 나옵니다. 같은 입력을 단위 테스트로 옮겨 재현합니다.

5. 새로 확인한 동작은 분석 문서에 측정 호스트(제조사, 모델)와 함께 확인됨/추정/미확정으로 적습니다. 지금 기록은 AMD Zen 3 하나입니다. **Intel과 P6 세대 실물의 결과가 가장 필요합니다.**

6. `--record <파일>`은 일치한 사례를 trace로 씁니다(허용 사례 제외). 저장소의 `tests/traces/simd.rxt`가 이렇게 만들어졌고 다섯 호스트의 ctest가 재생합니다([정수 fuzz와 trace 가이드](integer-host-fuzz-and-traces.md)).

*Build (Release for long runs, and an i386 process too), run long seeds with zero `mismatches` expected (`within_tolerance` counts the RCPPS/RSQRTPS approximation differences within the SDM's bound; `faults:` counts matching cases by `FaultKind`), narrow with `--only`, reproduce a `MISMATCH` (it prints the instruction bytes, memory, EAX, EFLAGS and the input FXSAVE image) as a unit test, and record new facts in the analysis topic with the host's vendor and model. Today's record is one AMD Zen 3; Intel and P6 hardware are what is most needed. `--record <file>` writes matching cases as traces (tolerated ones left out); `tests/traces/simd.rxt` was made that way and every host's ctest replays it.*

## 하네스의 경계 / What the harness leaves out

* 범용 레지스터 피연산자는 EAX만 씁니다(스텁이 EBX, ESI, EDI, EDX를 씀).
* MXCSR의 DAZ(비트 6)는 세우지 않습니다. 호스트에는 있고 Pentium III에는 없어 결과가 갈리기 때문입니다.
* FXSAVE/FXRSTOR 자체는 하네스라 대조 대상이 아니며, 단위 테스트가 맡습니다.
* 66/F2 접두 형태(SSE2 이후)는 뺍니다.

*General register operands are EAX only (the stub uses EBX, ESI, EDI, EDX); MXCSR's DAZ (bit 6) is never set, since the host has it and the Pentium III does not; FXSAVE/FXRSTOR themselves are the harness and unit tests cover them; 66/F2-prefixed forms (SSE2 and later) are left out.*
