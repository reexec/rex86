# #31 작업 로그 : 견고성 하네스 / #31 work log : the robustness harness

이슈: [#31](https://github.com/reexec/rex86/issues/31) | 설계: [20261009-i031](../design/20261009-i031-robustness-fuzz.md) | 지시서: [20261009-i031](../work-orders/20261009-i031-robustness-fuzz.md)

## 2026-10-09

- **출발점**: v0.0.16(1b 완료) 뒤, README 목표 6의 남은 측정 수단. ASan/UBSan CI 작업은 #22에서 이미 있었다.
- **먼저 확인한 것**: REP 문자열이 한 Step 안에서 끝까지 반복한다(`exec_strings.cpp`). 크래시가 아니라 반응성 문제이므로 설계 결정 4에 적고 후속 [#32](https://github.com/reexec/rex86/issues/32)를 만들었다. 하네스는 메모리를 1 MiB 이하로 둔다. 로컬 Clang 18의 libFuzzer가 ASan/UBSan과 함께 링크되는 것을 확인했다.
- **구현**:
  - `src/tools/robust/robust_case.{h,cpp}`: 난수원, 보호 구역(ASan에서는 poison), 페이지 속성, 상태, 기능, 임의로 답하는 `Environment`, 호스트 개입, 불변식 I2~I6, 디코더 fuzz, 검사기 시험용 사보타주.
  - `main.cpp`: 드라이버. `--case`는 Run마다 이벤트를 찍는다.
  - `libfuzzer.cpp`: libFuzzer 진입점과 CMake 옵션 `REX86_LIBFUZZER`.
  - ctest `rex86_robust_smoke`(모든 호스트), CI 작업 `linux-x64-libfuzzer`.
- **생성기 조정**: 첫 판은 300건에 660명령만 retire했다. 대부분 첫 명령에서 폴트였다. 다음을 고쳐 Release 50만 건에서 케이스당 약 140명령이 됐다.
  - 케이스를 온건한/적대적으로 나눴다.
  - 명령 수프의 ModRM 절반을 레지스터 형태로, 변위 바이트를 작게 했다.
  - Run 수를 늘리고, 인출 폴트 뒤에는 EIP를 다른 곳으로 옮긴다.
- **libFuzzer 조정**:
  - 첫 판은 입력 바이트가 생성기의 처음 몇 선택에만 쓰였다. 앞 16바이트는 생성기에, 나머지는 EIP의 코드로 넣도록 바꿨다.
  - 게스트 메모리를 64 KiB로 묶었다(`LLVMFuzzerInitialize`).
  - 메모리 채우기를 64비트 단위로 바꿨다.
- **MSVC**: 다이제스트에서 `bool | int`를 섞은 두 곳이 C4805(`/WX`)로 막혀, 명시적 비트 함수로 바꿨다.
- **단위 테스트** `tests/unit/robust_test.cpp`:
  - 같은 시드는 같은 다이제스트를 낸다. libFuzzer 바이트가 생성기를 이끈다.
  - 사보타주 셋(I3, I2, I5)을 검사기가 잡는다.
  - 40개 시드가 통과하고, 디코더 2,000건이 통과한다.
  - 결과: checks 1,091, failures 0.
- **결과**: 코어 결함 0, 위반 0, 새니타이저 보고 0. 규모는 [분석](../analysis/robustness-fuzz.md) 1절에 있다.
  - Release 50만 건(7,149만 명령).
  - ASan/UBSan 7만 건.
  - i386 1만 건.
  - MSVC 2만 건.
  - libFuzzer 40분(7만 4천 회).
- **빌드와 ctest**(`-Werror`): x86-64 Debug 7/7, i386 Debug 8/8, GCC ASan/UBSan 7/7, MSVC x86 Debug 5/5. Clang libFuzzer 빌드는 경고 없음. AArch64와 wasm32는 push 뒤 CI로 확인한다.
- **문서**: 설계, 지시서, 가이드 `robustness-fuzz.md`, 분석 `robustness-fuzz.md`와 색인, ARCHITECTURE(도구, 빌드, CI), README의 견고성 행.
- **남은 것**: 번역 백엔드의 차등 fuzz(3단계), wasm32/AArch64의 긴 실행, REP 정지 시간(#32).

*Starting point: after v0.0.16, the remaining instrument of README goal 6 (the ASan/UBSan job came with #22). Checked first: a REP string runs to completion within one Step, a responsiveness problem written into design decision 4 and handed to #32, the harness capping memory at 1 MiB; local Clang 18 links libFuzzer with ASan/UBSan. Implementation: `robust_case` (random source, guard zones poisoned under ASan, page attributes, state, features, a randomly answering `Environment`, host interventions, invariants I2-I6, the decoder fuzz, sabotage hooks for testing the checkers), the driver with `--case` printing every Run's event, the libFuzzer entry with `REX86_LIBFUZZER`, ctest `rex86_robust_smoke` on every host and the CI job `linux-x64-libfuzzer`. Generator tuning: the first version retired 660 instructions over 300 cases, nearly all ending at the first instruction; benign/hostile cases, register-form ModRMs and small displacements in the soup, more Runs and moving EIP after fetch faults bring a 500,000-case Release run to about 140 instructions a case. libFuzzer tuning: input bytes first fed only the generator's first choices, now 16 header bytes drive the generator and the rest is the code at EIP; guest memory is capped at 64 KiB (`LLVMFuzzerInitialize`) and filled 64 bits at a time. MSVC's C4805 under `/WX` caught two bool-int packings, replaced by an explicit bit helper. Unit tests: seed reproducibility, libFuzzer bytes driving the generator, the checkers catching the three sabotages (I3, I2, I5), 40 seeds and 2,000 decoder cases passing; 1,091 checks, zero failures. Result: no core defect, zero violations, zero sanitizer reports over 500,000 Release cases (71.5M instructions), 70,000 ASan/UBSan, 10,000 i386, 20,000 MSVC and 40 minutes of libFuzzer (74,000 executions). Builds and ctest with `-Werror`: x86-64 Debug 7/7, i386 Debug 8/8, GCC ASan/UBSan 7/7, MSVC x86 Debug 5/5, the Clang libFuzzer build warning-free; AArch64 and wasm32 go to CI after the push. Remaining: the translation backends' differential fuzz (phase 3), long wasm32/AArch64 runs, the REP stall (#32).*
