# SIMD 호스트 CPU 대조 / The SIMD host-CPU comparison

근거 작업: [#29](https://github.com/reexec/rex86/issues/29) ([설계](../design/20261009-i029-mmx-and-sse.md), [로그](../work-logs/20261009-i029-mmx-and-sse.md)) | 절차: [SIMD fuzz 가이드](../guides/simd-host-fuzz.md) | 배경: [MMX와 SSE](../kb/simd.md)

이 문서는 `rex86_simd_fuzz`와 작은 프로브로 호스트 CPU에서 확인한 MMX/SSE 동작을 누적한다. 측정 호스트는 2026-10-09 기준 **AMD Ryzen 5 5600X(Zen 3)** 하나다. Intel과 P6 세대 실물의 대조는 아직 없다.

*This topic accumulates MMX/SSE behavior verified on host CPUs with `rex86_simd_fuzz` and small probes. As of 2026-10-09 the only measuring host is an **AMD Ryzen 5 5600X (Zen 3)**; no Intel or P6-generation hardware has been compared yet.*

## 1. 대조 결과 / Comparison results

**확인됨**(Zen 3, WSL2 Ubuntu 24.04, GCC 13). 형태 7,961개(MMX, SSE, MXCSR, PREFETCH의 모든 인코딩 중 범용 레지스터 피연산자가 EAX인 것, 메모리 [ebx]와 [ebx+8]).

| 실행 | 건수 | 불일치 | 허용(RCP/RSQRT) |
|---|---|---|---|
| 증분 1(부동소수점 제외 5,917 형태), x86-64 Release, 시드 100~109 | 10,000,000 | 0 | — |
| 증분 1, i386 Debug, 시드 200~201 | 1,000,000 | 0 | — |
| 전체, x86-64 Release, 시드 300~309 | 10,000,000 | 0 | 표 아래 |
| 전체, i386 Debug, 시드 400~401 | 1,000,000 | 0 | 표 아래 |
| 그룹별 집중 실행(시드 7, 그룹마다 20만) | 2,000,000 | 0 | RCP/RSQRT 그룹 108,966 |

허용은 RCPPS/RSQRTPS와 스칼라형의 요소가 SDM 오차 한계 안에서 다른 경우다(설계 결정 6). 전체 실행에서 약 2.2%이며 모두 이 네 명령이다.

*Confirmed on Zen 3 (WSL2 Ubuntu 24.04, GCC 13) over 7,961 forms (every MMX, SSE, MXCSR and PREFETCH encoding with EAX as the only general register and [ebx]/[ebx+8] as memory), as in the table: zero mismatches everywhere. Tolerated cases are RCPPS/RSQRTPS and their scalar forms differing within the SDM's bound (design decision 6), about 2.2% of the full runs and all from those four.*

## 2. FXSAVE와 MMX 상태 / FXSAVE and the MMX state

| 항목 | 결과 | 상태 |
|---|---|---|
| MXCSR_MASK | Zen 3는 0x0002FFFF(DAZ와 AMD의 비정렬 SSE 비트 17). 코어는 Pentium III의 0x0000FFBF | 확인됨(호스트마다 다름, 비교하지 않음) |
| x87 예외가 없을 때의 FIP/FDP/FOP | Zen 3는 0을 저장 | 확인됨(AMD 동작, 비교하지 않음). Intel에서는 미확정 |
| 레지스터 슬롯의 바이트 10~15 | 0 | 확인됨 |
| MMX 명령 뒤 | TOP = 0, 축약 태그 0xFF, 쓴 레지스터의 79:64 = FFFF | 확인됨, SDM 표 9-2와 같음 |
| **EMMS** | 태그를 비우고 **TOP도 0** | 확인됨, SDM 표 9-2와 같음. 첫 구현은 TOP을 두었다가 fuzz로 고쳤다 |
| CVTPI2PS의 메모리 원본 | MMX 전환(TOP, 태그)을 하지 **않는다**. MM 레지스터 원본만 한다 | 확인됨(집중 실행 20만 건) |
| CVTPS2PI/CVTTPS2PI | 언제나 MMX 전환 | 확인됨 |

*Zen 3 writes MXCSR_MASK 0x0002FFFF where the core writes the Pentium III's 0x0000FFBF, and stores zero FIP/FDP/FOP without a pending exception (AMD behavior; neither is compared, Intel's FIP side unresolved); register slots' bytes 10-15 are zero; an MMX instruction leaves TOP 0, abridged tag 0xFF and FFFF in bits 79:64; **EMMS empties the tags and zeroes TOP too** (SDM table 9-2; the first implementation kept TOP and the fuzz caught it); CVTPI2PS with a memory source makes **no** MMX transition, an MM source does; CVTPS2PI/CVTTPS2PI always do. All confirmed.*

## 3. SSE 부동소수점 / SSE floating point

프로브와 fuzz로 확인한 것. SDM이 분명한 곳은 SDM과 일치를 확인했고, SDM이 침묵하거나 x87과 다른 곳은 측정을 따랐다.

| 항목 | 결과 | 상태 |
|---|---|---|
| 언더플로 판정 시점 | **반올림 전**. 0x00FFFFFF × 0.5 = 0x007FFFFF.8이 0x00800000으로 반올림돼도 #U(+#P). x87은 반올림 뒤 | 확인됨 |
| 마스크된 #U | 결과가 정확하면 세우지 않음(정확한 비정규 결과는 플래그 없음) | 확인됨 |
| 마스크 안 된 #U | 정확해도 #XM | 확인됨 |
| FZ | 작은 결과를 ±0으로, 정확해도 #U와 #P | 확인됨 |
| NaN과 비정규 입력 | NaN 피연산자가 있으면 다른 쪽이 비정규여도 #D 없음 | 확인됨 |
| #I/#Z와 #D | 같은 요소에서 무효 연산(음의 비정규수 제곱근)이나 0 나누기(비정규수/0)가 나면 #D 없음. SDM 11.5.2의 우선순위 | 확인됨. 첫 구현은 #D를 세웠다가 fuzz로 고쳤다 |
| 두 NaN | 첫 피연산자를 QNaN으로. QNaN,SNaN이면 첫 QNaN과 #I | 확인됨, SDM 표 4-7 |
| MINPS/MAXPS | NaN이 하나라도 있거나 둘 다 0이면 둘째를 그대로, QNaN에도 #I | 확인됨, SDM 의사 코드 |
| CMPPS | LT/LE/NLT/NLE는 QNaN에도 #I, 나머지는 SNaN에만. imm8은 하위 3비트만 | 확인됨(fuzz는 imm8 전체를 무작위로) |
| 패킹의 마스크 안 된 예외 | 전계산이면 전계산 플래그만(예: #I+#D), 후계산이면 전부(예: #D+#O+#P), 결과는 쓰지 않음 | 확인됨, SDM 11.5.1 |
| 스칼라형 | 요소 0만 평가, 위 요소의 SNaN은 아무것도 일으키지 않음 | 확인됨 |

*Probes and the fuzz confirmed: **tininess is detected before rounding** (0x007FFFFF.8 rounding to 0x00800000 still raises #U with #P, unlike the x87); masked #U is not raised for an exact result; unmasked #U is #XM even when exact; FZ flushes tiny results to ±0 with #U and #P even when exact; a NaN operand suppresses #D for the other operand's denormal; an invalid operation (the square root of a negative denormal) or a division by zero (a denormal over zero) suppresses #D in that lane, as SDM 11.5.2's precedence says (the first implementation raised #D and the fuzz caught it); two NaNs give the first, quieted, with #I for any SNaN (SDM table 4-7); MINPS/MAXPS return the second operand as it is for any NaN or two zeros, with #I on QNaNs too; CMPPS signals on QNaNs for LT/LE/NLT/NLE only and uses imm8's low 3 bits; unmasked exceptions in packed operations set only the pre-computation flags when one of those is unmasked, else every flag, writing nothing (SDM 11.5.1); scalar forms look at lane 0 alone.*

## 4. RCPPS/RSQRTPS

| 입력 | Zen 3 | 코어 | 상태 |
|---|---|---|---|
| 1.0의 RCP | 0x3F7FF000 | 0x3F800000 | 근사값의 차이(허용) |
| 비정규수, ±0 | ±∞ | ±∞ | 확인됨(비정규수를 0으로 봄) |
| ±∞ | ±0 (RSQRT의 −∞는 기본 NaN) | 같음 | 확인됨 |
| 음수의 RSQRT | 0xFFC00000, 플래그 없음 | 같음 | 확인됨 |
| NaN | QNaN으로 | 같음 | 확인됨 |
| RCP의 입력 2^126 | 0(작은 결과로 플러시) | 2^-126(0x00800000) | SDM이 구현 정의로 둔 구간. 허용 |
| RCP의 입력 > 2^126 | 0 | 0 | 확인됨 |

SDM은 상대 오차 1.5 × 2⁻¹²만 정하고 Intel과 AMD의 값이 다르다. 코어는 RCP를 binary32 나눗셈으로 정확히 반올림하고, RSQRT를 binary64의 제곱근과 나눗셈 뒤 binary32로 반올림한다. 결과는 모든 호스트에서 같다(목표 2). fuzz는 유한한 결과끼리 상대 차이 2⁻¹¹ 이하, 또는 SDM이 정한 RCP 구현 정의 구간에서 한쪽이 0인 경우를 허용한다. Intel 실물의 값은 **미확정**이다.

*The SDM bounds the relative error at 1.5 × 2⁻¹² and Intel and AMD differ; the core rounds RCP correctly through a binary32 division and RSQRT through a binary64 square root and division rounded to binary32, the same bits on every host (goal 2). The fuzz tolerates finite results within 2⁻¹¹ relatively, and a zero on one side in the SDM's implementation-defined RCP band; special values match exactly as the table shows. Intel's values are unresolved.*

## 5. 미확정 / Unresolved

* **Intel과 P6 세대 실물**: 위 모든 항목. 특히 EMMS의 TOP(SDM은 0), CVTPI2PS 메모리 원본의 MMX 전환, RCP 구간의 경계, FIP/FDP 저장. 확인 방법: 그 호스트에서 `rex86_simd_fuzz` 긴 실행(가이드).
* **SSE가 꺼진 FXSAVE**(Pentium II, Mendocino): MXCSR과 XMM 영역을 건드리지 않는다는 코어의 동작은 SDM의 "예약"에서 끌어낸 **추정**이다. 지금의 호스트는 모두 SSE가 있어 잴 수 없다.
* **MASKMOVQ의 선택되지 않은 바이트**: 코어는 쓰는 바이트만 검사한다. fuzz의 [EDI]는 늘 매핑되어 있어 폴트 쪽은 재지 못했다(**추정**).

*Unresolved: Intel and P6 hardware for everything above (EMMS's TOP, CVTPI2PS's memory-source transition, the RCP band, FIP/FDP storage), checked by long `rex86_simd_fuzz` runs there; FXSAVE without SSE (Pentium II, Mendocino) leaving MXCSR and the XMM area alone, inferred from the SDM's "reserved" and unmeasurable on today's hosts; MASKMOVQ's unselected bytes, the core checking only the bytes it writes, the fault side unmeasured since the fuzz's [EDI] is always mapped (inferred).*
