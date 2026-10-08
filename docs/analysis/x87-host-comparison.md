# x87 호스트 CPU 대조에서 확인된 사실 / Facts from the x87 host-CPU comparison

근거 작업: [#19](https://github.com/reexec/rex86/issues/19) ([설계](../design/20261008-i019-x87-increment-1.md), [로그](../work-logs/20261008-i019-x87-increment-1.md)), [#22](https://github.com/reexec/rex86/issues/22)(Intel 실행, [로그](../work-logs/20261008-i022-integer-host-fuzz-and-trace.md)) | 도구: `rex86_x87_fuzz`([가이드](../guides/x87-host-fuzz.md)) | 측정 호스트: AMD Ryzen 5 5600X(Zen 3, #19), Intel Xeon Cascade Lake(family 6 model 85 stepping 7, #22), Linux x86-64

상태 표기: **확인됨**(호스트에서 같은 바이트를 실행해 일치를 확인), **추정**, **미확정**. #22에서 Intel 호스트로 다시 돌려, 아래의 "확인됨"은 따로 적지 않은 한 **두 제조사 모두**에서의 확인이다.

*#22 reran the comparison on an Intel host: unless a line says otherwise, every "confirmed" below holds on **both vendors**.*

## 1. 방법 / Method

호스트와 코어가 **같은 기계어**를 실행한다: `push edx; popf; frstor [edi]; <명령>; fnsave [esi]; pushf; pop edx`. 입력은 무작위 FRSTOR 이미지(특수값에 비중), 무작위 CW(마스크, 반올림, 정밀도), EFLAGS, 128바이트 메모리 피연산자다. FNSAVE 이미지(FIP/FDP 제외), 메모리, 산술 플래그, EAX를 비교한다. 초월함수와 FISTTP를 뺀 473개 인코딩(레지스터 형식 전부와 `[ebx]` 메모리 형식, 16비트 환경 형식 포함)을 대상으로 했다.

결과: AMD Zen 3(#19, 시드 200~219 × 200만) 4,000만 건 **불일치 0**, 3절의 비교 이탈 251,332건(0.63%)은 별도로 센다. Intel Cascade Lake(#22, 시드 200~209 × 200만) 2,000만 건 **불일치 0**, 같은 비교 이탈 125,452건(0.63%). Intel 첫 실행에서 나온 9건(약 100만 건에 1건)은 3절의 두 번째 항목이고, 코어를 SDM 표에 맞게 고친 뒤 0이 됐다.

*Host and core run the **same machine code** (`push edx; popf; frstor [edi]; <instruction>; fnsave [esi]; pushf; pop edx`) from a random FRSTOR image weighted toward special values, a random CW, EFLAGS and a 128-byte memory operand, comparing the FNSAVE image (FIP/FDP excluded), memory, the arithmetic flags and EAX over 473 encodings: every register form, the `[ebx]` memory forms and the 16-bit environment forms, transcendentals and FISTTP excluded. AMD Zen 3 (#19, seeds 200-219 × 2M): 40M cases with **zero mismatches**, section 3's compare deviation counted apart (251,332 cases, 0.63%). Intel Cascade Lake (#22, seeds 200-209 × 2M): 20M cases with **zero mismatches** and the same compare deviation (125,452, 0.63%); the 9 cases (about 1 in a million) of the first Intel run are section 3's second item, gone once the core followed the SDM tables.*

## 2. SDM이 정하지 않은 곳의 실측값(코어에 반영) / Measured where the SDM is silent (adopted)

코어가 아래 값을 따른다. SDM이 "미정의"로 두거나 아무 말도 하지 않는 곳이라, 결정적인 실측값을 채택해도 SDM과 충돌하지 않는다(#13의 원칙).

| 항목 | 확인된 동작 |
|---|---|
| CW 적재 | FLDCW/FLDENV/FRSTOR는 CW를 `(v & 0x1F3F) \| 0x0040`으로 저장한다(비트 7, 13~15는 0, 비트 6은 1). 정밀도 제어 01(예약)은 64비트(확장)처럼 동작한다 |
| 환경 이미지 | 32비트 형식의 예약 상위 절반은 `0xFFFF`. FLDENV/FRSTOR는 태그에서 "비었음 여부"만 쓰고 나머지 태그는 레지스터 내용에서 다시 계산한다 |
| 조건 코드 | FCOMI 계열과 FCMOVcc는 C1을 바꾸지 않는다. FFREE/FFREEP와 FSTP(NCE)는 C1을 0으로. FPREM/FPREM1: 부분 감소면 C2=1, C0=C1=C3=0. NaN, 무효, 마스크 안 된 예외로 멈추면 C1=C2=0이고 C0/C3는 유지 |
| 스택 폴트 우선순위 | FLD ST(i), FXTRACT: ST(0)/원본이 비어 있으면 스택이 꽉 차 있어도 언더플로(C1=0)로 보고한다 |
| FCMOVcc 언더플로 | 마스크된 응답은 조건과 무관하게 ST(0)에 indefinite를 쓴다 |
| FSTPNCE (D9 D8+i) | Intel XED의 이름 그대로 "빈 레지스터 검사 없음": ST(0)가 비어 있으면 폴트, 복사 없이 pop만 한다(C1=0) |
| 저장 명령의 #D | FST/FIST/FBSTP는 denormal 원본에 #D를 내지 않는다(SDM의 예외 목록과 일치) |
| FLD m32/m64의 #D | #D가 마스크되지 않아도 denormal을 적재한다(ES는 설정) |
| 비교의 #D | #D가 마스크되지 않아도 조건 코드는 실제 비교 결과로 설정되고, pop만 보류된다 |
| 예외 우선순위 | SDM 4.9.2 그대로: 스택 폴트 > 지원하지 않는 형식, SNaN > QNaN 피연산자 > 그 밖의 무효 연산, 0으로 나눔 > #D > #O/#U > #P. 메모리 피연산자의 #IA/#D도 이 순서에 들어간다(변환 시점이 아니라 연산 시점에 판정). SNaN 메모리 피연산자는 SNaN으로 NaN 선택 규칙에 참여한다 |
| pseudo-denormal | 지수 1의 같은 유효숫자 값으로 계산된다. 피연산자를 그대로 돌려줄 때(x rem ∞)도 정규형(지수 1)으로 돌아온다 |
| FPREM/FPREM1 부분 감소 | 지수 차 D ≥ 64이면 N = 32 + (D mod 32)비트만큼 줄인다(직접 측정 800건 + fuzz). FPREM1도 부분 감소에서는 몫을 0 방향으로 자른다 |
| 피연산자를 그대로 돌려주는 결과 | x rem ∞(FPREM/FPREM1)와 0배 FSCALE은 SDM 표대로 ST(0)를 돌려준다. x가 denormal이고 #U가 마스크되지 않아도 #U 없이 그대로다(Intel에서 확인, #22). AMD는 3절 |
| 이중 범위 초과 | #O/#U가 마스크되지 않았는데 2^24576 조정 후에도 범위를 벗어나면 반올림 모드와 무관하게 ±∞(C1=1) 또는 ±0(C1=0) (FSCALE에서 측정) |
| FBLD의 9 초과 숫자 | 니블 값 그대로 가중합에 들어간다(fuzz에서 무작위 BCD로 불일치 0) |

*Adopted by the core; the SDM leaves these undefined or says nothing, so a deterministic measured value conflicts with nothing (#13's rule). CW loads store `(v & 0x1F3F) | 0x0040`, and the reserved precision control 01 behaves as extended. The 32-bit environment's reserved halves are `0xFFFF`; FLDENV/FRSTOR keep only "empty or not" from the tags and recompute the rest. FCOMI and FCMOVcc leave C1 alone; FFREE/FFREEP and FSTP(NCE) clear it; a partial FPREM/FPREM1 reports C2=1 and C0=C1=C3=0; a NaN, invalid or unmasked-exception stop clears C1 and C2 and keeps C0/C3. FLD ST(i) and FXTRACT report an empty source as underflow (C1=0) even with a full stack. A masked FCMOVcc underflow writes the indefinite whatever the condition. FSTPNCE (D9 D8+i) is "no check empty" as Intel's XED names it: an empty ST(0) is popped without a fault or a copy. Stores raise no #D (matching the SDM's lists); FLD m32/m64 loads a denormal even with #D unmasked; a compare with #D unmasked still reports the real relation, withholding only the pops. Exception priority is SDM 4.9.2's, memory operands' #IA/#D included (decided at the operation, not the conversion), signaling memory NaNs taking part in NaN selection. Pseudo-denormals compute as the same significand at exponent 1, and come back in that canonical form when passed through (x rem ∞). Partial FPREM/FPREM1 reduce by N = 32 + (D mod 32) bits (800 direct measurements plus the fuzz), FPREM1 truncating there too. x rem ∞ and FSCALE by zero return ST(0) as the SDM tables say, a denormal x included, without #U even when #U is unmasked (confirmed on Intel, #22; AMD in section 3). With #O/#U unmasked, a result still out of range after the 2^24576 adjustment saturates to ±∞ (C1=1) or ±0 (C1=0) regardless of rounding (measured on FSCALE). FBLD's digits above 9 enter with their nibble values.*

## 3. SDM과 다른 하드웨어 동작(반영하지 않음) / Hardware deviating from the SDM (not adopted)

* **확인됨(AMD Zen 3, Intel Cascade Lake): 마스크 안 된 #IA의 비교 결과.** SDM의 FCOM/FCOMI 결과 표는 "unordered* … *Flags not set if unmasked invalid-arithmetic-operand (#IA) exception is generated"라고 적는다. AMD Zen 3는 마스크되지 않은 #IA에서도 C3/C2/C0 = 111(FCOMI 계열은 ZF/PF/CF = 111, OF/SF/AF = 0)을 쓴다. pop은 양쪽 모두 보류된다. Intel Cascade Lake도 같다(#22). 코어는 SDM을 따르고, fuzz는 이 경우를 `vendor_deviations`로 따로 센다.
* **확인됨(AMD Zen 3만): 그대로 돌려주는 denormal의 #U.** x rem ∞와 0배 FSCALE에서 ST(0)가 denormal이고 #U가 마스크되지 않았을 때, AMD Zen 3는 #U를 내고 지수 조정(2^24576) 값을 저장한다. SDM 표는 결과를 ST(0)로 적고, Intel은 #U 없이 ST(0)를 돌려준다. 코어는 #19에서 AMD를 따랐다가 #22에서 SDM과 Intel로 바꿨고, fuzz는 AMD의 이 응답을 `vendor_deviations`로 센다.

*The SDM's FCOM/FCOMI result tables say "flags not set if unmasked #IA"; the AMD Zen 3 writes unordered anyway (C3/C2/C0 = 111, or ZF/PF/CF = 111 with OF/SF/AF cleared), both sides withholding the pops, and so does the Intel Cascade Lake (#22). The core follows the SDM and the fuzz counts the case as `vendor_deviations`. Second, on the AMD Zen 3 alone: when x rem ∞ or FSCALE by zero returns a denormal ST(0) with #U unmasked, it raises #U and stores the 2^24576-adjusted value, where the SDM tables give ST(0) and the Intel returns it without #U. The core followed AMD in #19 and the SDM and Intel from #22; the fuzz counts AMD's response as `vendor_deviations`.*

## 4. 코어 구현에서 드러난 SoftFloat의 한계 / SoftFloat limits found on the way

* **확인됨**: SoftFloat 3e의 `extF80_add`는 두 pseudo-denormal의 덧셈에서 유효숫자의 올림을 잃는다(지수 0 그대로 계산). 코어는 SoftFloat에 넘기기 전에 pseudo-denormal을 지수 1로 정규화한다. unnormal, pseudo-NaN, pseudo-infinity는 SoftFloat이 정규화해 계산하지만 387 이후 x87에서는 무효 피연산자이므로 코어가 먼저 걸러낸다.

*Confirmed: SoftFloat 3e's `extF80_add` loses the significand's carry when adding pseudo-denormals (computing at exponent 0), so the core canonicalizes them to exponent 1 first; SoftFloat would normalize unnormals, pseudo-NaNs and pseudo-infinities, which the 387+ x87 rejects as invalid, so the core filters them first.*

## 5. 미확정 / Unresolved

* 한 세대의 두 제조사 외의 프로세서(P6 세대 실물 등). 확인 방법: 그 호스트에서 `rex86_x87_fuzz`를 긴 시드로 실행한다(가이드).
* 초월함수(FSIN 등): 2차 증분의 범위.
* FIP/FCS/FOP/FDP/FDS: 호스트 주소라 비교에서 뺐다. 코어의 값은 단위 테스트로만 확인했다.

*Unresolved: processors beyond these two (P6-generation hardware, for instance: run `rex86_x87_fuzz` with long seeds there, per the guide); the transcendentals, increment 2's scope; and FIP/FCS/FOP/FDP/FDS, excluded from the comparison as host addresses and checked only by unit tests.*
