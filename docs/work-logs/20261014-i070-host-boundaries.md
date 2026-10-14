# #70 작업 로그 : 호스트 경계 명령 / #70 work log : host boundary instructions

이슈: [#70](https://github.com/reexec/rex86/issues/70) | 설계: [20261014-i070](../design/20261014-i070-host-boundaries.md) | 지시서: [20261014-i070](../work-orders/20261014-i070-host-boundaries.md)

## 2026-10-14

브랜치는 #69와 같은 `work/i069-user-mode-privilege`다(작업 브랜치에서는 새 이슈도 이어서 한다).

*Same branch as #69, `work/i069-user-mode-privilege` (a work branch carries the next issue too).*

* 지시서 1~4를 구현했다. 판단 근거는 설계에 있다.
* Windows x86 MSVC Debug: `rex86_unit_tests` checks=2518 failures=0.
* WSL Linux x64 GCC 13.3 Debug(`-Werror`): 처음에는 `bench/runner.cpp`의 `StopReason` switch가 새 값을 다루지 않아 `-Wswitch`로 실패했다(MSVC는 경고하지 않음). `probe/main.cpp`에도 같은 switch가 있어 둘 다 고쳤다. 그 뒤 전체 빌드가 경고 없이 통과했고 checks=2502 failures=0.

*Work-order items 1 to 4 are done, reasoned in the design. Windows x86 MSVC Debug: checks=2518, failures=0. WSL Linux x64 GCC 13.3 Debug with `-Werror` first failed on `-Wswitch` in `bench/runner.cpp`, whose `StopReason` switch lacked the new value (MSVC did not warn); `probe/main.cpp` had the same switch, and both were fixed. The whole build then passed without warnings, checks=2502, failures=0.*
