# #69 작업 로그 : 사용자 모드 특권 의미 / #69 work log : user-mode privilege semantics

이슈: [#69](https://github.com/reexec/rex86/issues/69) | 설계: [20261014-i069](../design/20261014-i069-user-mode-privilege.md) | 지시서: [20261014-i069](../work-orders/20261014-i069-user-mode-privilege.md)

## 2026-10-14

브랜치는 `work/i069-user-mode-privilege`(main `beb3de6`, v0.0.33에서)다. 계기는 rePIU의 `emulated` 실행 모델([rePIU #61](https://github.com/reexec/rePIU/issues/61))이다.

*Branch `work/i069-user-mode-privilege` from main `beb3de6` (v0.0.33), prompted by rePIU's `emulated` execution model (rePIU #61).*

### 구현 / Implementation

* `Features::user_mode`(기본 꺼짐).
* `interp::ExecuteDecoded`: `FeatureEnabled` 뒤에 `RefusedInUserMode`(mnemonic 하나로 분류)를 둔다. `CLI`, `STI`, `HLT`, `IN`, `OUT`, `INS*`, `OUTS*`는 `kPrivilegedInstruction`(주소 = 명령의 선형 주소)이고, `INT imm8`은 `kGeneralProtection`(주소 자리에 오류 코드 벡터×8+2)이다. 상태를 저장하기 전에 돌아가므로 실행 전 폴트이고, 포트 콜백은 불리지 않는다.
* `PopWritableFlags(user_mode)`(`flags.h`): `POPF`/`POPFD`와 `IRET`/`IRETD`의 쓰기 마스크에서 IF와 IOPL을 뺀다. `interpreter.cpp`의 `kPopfWritable` 별칭은 쓰는 곳이 없어져 지웠다.
* 번역 경로는 바꾸지 않았다. IR 프런트엔드가 이 명령들을 번역하지 않는다는 것을 테스트(평가기, 문턱 0, 블록 고리 뒤 `CLI`)로 고정했다.

*`Features::user_mode` (off by default). In `interp::ExecuteDecoded`, `RefusedInUserMode` (classified by mnemonic alone) follows `FeatureEnabled`: `CLI`, `STI`, `HLT`, `IN`, `OUT`, `INS*` and `OUTS*` give `kPrivilegedInstruction` at the instruction's linear address, and `INT imm8` gives `kGeneralProtection` with the error code vector×8+2 in the address field. It returns before the state is saved, so the fault comes before retiring, and no port callback is made. `PopWritableFlags(user_mode)` in `flags.h` drops IF and IOPL from the `POPF`/`POPFD` and `IRET`/`IRETD` write masks; the `kPopfWritable` alias in `interpreter.cpp` lost its last user and was removed. The translation path is unchanged; a test (the evaluator at threshold 0, `CLI` after a block loop) pins that the IR frontend leaves these instructions alone.*

### 검증 / Verification

| 호스트 / Host | 빌드 / Build | 결과 / Result |
|---|---|---|
| Windows 11, MSVC x86 | `windows-x86-debug` | `rex86_unit_tests` checks=2498 failures=0, `rex86_probe` result=ok |
| WSL2 Ubuntu 24.04, GCC 13.3 x64 | `linux-x64-debug` (`-Werror`) | `rex86_unit_tests` checks=2482 failures=0 |

새 테스트 `user_mode_test.cpp`의 첫 실행에서 실패가 하나 있었다. 블록 고리 테스트의 기대값(ECX 0x101)이 틀렸다. 고리 순서를 `inc ecx; cmp ecx, 0x100; jnz`로 고친 뒤 끝값은 0x100이다. 구현 문제가 아니라 테스트를 쓰면서 낸 실수다.

*The first run of the new `user_mode_test.cpp` had one failure: the block-loop test's expected value (ECX 0x101) was wrong. After the loop was reordered to `inc ecx; cmp ecx, 0x100; jnz`, the final value is 0x100. It was a mistake in writing the test, not in the implementation.*

i386, AArch64, wasm32는 CI로 확인한다.

*i386, AArch64 and wasm32 are left to CI.*
