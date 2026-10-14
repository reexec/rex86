# #69 작업 지시 : 사용자 모드 특권 의미 / #69 work order : user-mode privilege semantics

이슈: [#69](https://github.com/reexec/rex86/issues/69) | 설계: [20261014-i069](../design/20261014-i069-user-mode-privilege.md) | 로그: [20261014-i069](../work-logs/20261014-i069-user-mode-privilege.md)

## 작업 항목 / Tasks

1. `include/rex86/environment.h`: `Features::user_mode`(기본 `false`)와 주석.
2. `src/interp/interpreter.cpp`: `ExecuteDecoded`의 `FeatureEnabled` 뒤에 사용자 모드 검사(결정 2·3). `POPF`/`POPFD`의 쓰기 마스크.
3. `src/interp/exec_segments.cpp`: `IRET`/`IRETD`의 쓰기 마스크.
4. `tests/unit/user_mode_test.cpp`와 등록(`main.cpp`, CMake).
5. 빌드와 `rex86_unit_tests`, `rex86_probe`: Windows x86 MSVC, WSL Linux x64.
6. 로그, README 달성도 표의 2A 행에 한 줄.

*1. `Features::user_mode` (default `false`) with its comment. 2. The user-mode check after `FeatureEnabled` in `ExecuteDecoded` (decisions 2 and 3) and the `POPF`/`POPFD` write mask. 3. The `IRET`/`IRETD` write mask. 4. `tests/unit/user_mode_test.cpp` and its registration. 5. Build and run `rex86_unit_tests` and `rex86_probe` on Windows x86 MSVC and WSL Linux x64. 6. The log and one line in the README's 2A row.*

## 완료 기준 / Done when

새 테스트와 기존 테스트 전체 통과, 기본값에서 동작 변화 없음.

*The new tests and every existing test pass, with no change in behaviour at the default.*
