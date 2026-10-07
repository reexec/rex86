# #13 설계 : 인터프리터 2차 — 시프트/회전, 곱셈/나눗셈, 문자열, 비트 연산 / #13 design : interpreter increment 2 — shifts, multiply/divide, strings and bit operations

이슈: [#13](https://github.com/reexec/rex86/issues/13) | 지시서: [20261007-i013](../work-orders/20261007-i013-interpreter-increment-2.md) | 로그: [20261007-i013](../work-logs/20261007-i013-interpreter-increment-2.md)

## 범위 / Scope

[#11 설계](20261007-i011-interpreter-core.md)의 구조(실행 코어와 의미의 분리, SST 실행 비교 합격 기준)는 그대로이고, 이 작업은 명령 그룹만 더한다. 1차에서 미구현 757,242건의 최대 버킷이다.

| 그룹 | 명령 | 의미 주의점 |
|---|---|---|
| 시프트 | SHL/SHR/SAR (C0/C1/D0~D3) | count는 `& 0x1F`(386+). count 0이면 플래그 무변경. CF는 마지막으로 밀려난 비트, OF는 count 1에서만 정의(SHL: CF^최상위, SHR: 원래 최상위, SAR: 0). count>1의 OF와 count≠0의 AF는 미정의 → 결정적 값(386 관측 또는 0)으로 두고 RM32 마스크가 가린다 |
| 회전 | ROL/ROR/RCL/RCR | RCL/RCR은 CF 포함 (width+1)비트 회전, count는 `& 0x1F` 뒤 RCL/RCR은 추가로 `% (width+1)`. CF 갱신, OF는 count 1에서만 정의 |
| 더블 시프트 | SHLD/SHRD (0FA4/A5/AC/AD) | count `& 0x1F`. 16비트 피연산자에 count>16은 미정의 동작 → 결정적으로 처리 |
| 곱셈 | MUL, IMUL(1·2·3 피연산자) | CF=OF=상위 절반 유효 여부(IMUL은 부호 확장 일치 여부). SF/ZF/AF/PF 미정의 → 하위 결과 기준 결정적 계산 |
| 나눗셈 | DIV, IDIV | 0 나누기와 몫 범위 초과는 `kFault`/`kDivide`(retire 없음, EIP 보존). 플래그 전부 미정의 |
| 문자열 | MOVS/STOS/LODS/SCAS/CMPS (+REP/REPE/REPNE) | DF 방향, 주소 크기에 따라 SI/DI/CX 또는 ESI/EDI/ECX. REP는 반복마다 메모리 접근과 카운터 갱신(폴트 시 중간 상태가 아키텍처 상태). 구현은 한 Step 안에서 반복을 완료하되 예산은 명령 1개로 센다(1차 루프 계약 유지) |
| 비트 | BT/BTS/BTR/BTC (레지스터·즉치 오프셋, 메모리 비트 기준 주소 조정 포함), BSF/BSR | CF=선택 비트. 메모리 피연산자의 비트 오프셋은 바이트 경계 밖으로 나간다(SDM의 bit base). BSF/BSR: 소스 0이면 ZF=1, 목적지 미정의(386: 유지로 관측될 수 있음 → 결정적으로 '유지') |
| 기타 | SETcc(0F90~9F), ENTER/LEAVE, XLAT | SETcc는 1차의 조건 평가 재사용. ENTER의 nesting level은 `% 32`, 0 아닌 nesting은 드물지만 SDM대로 |

* 미정의 플래그·값의 결정 원칙: **모든 호스트에서 비트 단위 동일**이 요구이므로, SDM이 미정의로 두는 곳은 (a) SST로 386 동작이 확인되면 그 값을, (b) 아니면 가장 단순한 결정적 값을 쓴다. 어느 쪽이든 RM32 마스크 밖에서 비교 불일치가 나면 그 명령을 로그에 적고 값을 조정한다.
* 세그먼트 적재(MOV sreg/LES/LDS, far 제어 흐름)와 x87, BCD(AAA/AAS/AAM/AAD/DAA/DAS)는 다음 증분이다.
* 공개 계약 무변경, 소비자 영향 없음.

## 검증 / Verification

* 단위 테스트: 그룹별 대표 케이스(시프트 count 0/1/큰 값, RCR CF 경유, MUL/IMUL 상·하위, DIV 폴트와 EIP 보존, REP MOVS 방향·겹침, BT 메모리 비트 베이스, BSF 0 소스).
* SST 실행 비교: 전체 스위트에서 **mismatches 0** 유지. 커버리지(실행 비율) 수치를 로그에 기록.
* 로컬 Windows x86 빌드와 `ctest`, 나머지 호스트는 push 시 CI.

*Scope: increment 2 adds instruction groups onto #11's unchanged structure — shifts (count masked by 0x1F, flags untouched at count 0, CF the last bit shifted out, OF defined only at count 1), rotates (RCL/RCR rotate through CF over width+1 bits with the extra modulo), SHLD/SHRD, MUL/IMUL (CF=OF = upper-half significance) and DIV/IDIV (divide errors fault with kDivide, no retire, EIP preserved), the string instructions with REP/REPE/REPNE (DF direction, address-size-selected registers, iterations completed within one Step but budgeted as one instruction), the bit operations (BT family with the memory bit base crossing byte boundaries; BSF/BSR with ZF on zero sources and a deterministic 'preserve' destination), SETcc reusing #11's condition evaluation, ENTER/LEAVE and XLAT. Where the SDM leaves flags or values undefined, bit-identical-across-hosts demands a deterministic choice: the 386-observed value where SST confirms one, else the simplest deterministic value, adjusted when a comparison mismatch outside the RM32 masks names the instruction. Segment loads, far control flow, x87 and BCD are the next increment; no public-contract change. Verification: representative unit cases per group, zero mismatches across the full SST suite with the coverage ratio recorded, the local Windows x86 build and ctest, CI for the other hosts on push.*
