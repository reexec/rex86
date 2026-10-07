# #1 작업 지시서 : 저장소 구조, 개발 규칙, 공개 계약 / #1 work order : repository structure, development rules and the public contract

이슈: [#1](https://github.com/reexec/rex86/issues/1) | 설계: [20261007-i001](../design/20261007-i001-repository-and-public-contract.md) | 로그: [20261007-i001](../work-logs/20261007-i001-repository-and-public-contract.md)

## 절차 / Steps

1. 저장소 규칙: `AGENTS.md`(rePIU 규칙의 CPU 코어 부분집합, 작업 처리와 이슈 연동, 브랜치와 머지 절차 포함), `CLAUDE.md`, `docs/CODING_STYLE.md`, `docs/PROJECT_CHARTER.md`, `LICENSE`(BSD 3-Clause), `VERSION`(0.0.1), `.gitignore`, `THIRD_PARTY_NOTICES.md`, `docs/analysis/README.md`와 `docs/kb/README.md` 색인, `docs/release-notes/README.md`.
   *Repository rules: `AGENTS.md` (the CPU-core subset of rePIU's rules with task handling, issue integration and the branch and merge procedure), `CLAUDE.md`, `docs/CODING_STYLE.md`, `docs/PROJECT_CHARTER.md`, `LICENSE`, `VERSION`, `.gitignore`, `THIRD_PARTY_NOTICES.md`, the analysis and kb indices, `docs/release-notes/README.md`.*
2. 공개 계약: `include/rex86/cpu_state.h`, `guest_memory.h`, `environment.h`, `cpu.h`, `version.h`와 `src/`의 구현. `Run`은 `kNoEngine`을 보고한다.
   *The public contract and its implementation; `Run` reports `kNoEngine`.*
3. 하네스: `tests/unit/test_support.h`와 네 테스트, `src/tools/probe/`의 `rex86_probe`.
   *The harness and the probe.*
4. 빌드: `CMakeLists.txt`(`rex86::core`, `REX86_BUILD_TESTS`, `REX86_WARNINGS_AS_ERRORS`), `CMakePresets.json`, `scripts/build_web_wasm.sh`.
   *Build files.*
5. CI: `.github/workflows/ci.yml`의 다섯 작업(Windows x86, Linux x64 GCC와 Clang, Linux i386 컨테이너, Linux AArch64, wasm32).
   *CI with five jobs.*
6. `README.md`, `ARCHITECTURE.md`, 작업 로그.
   *README, ARCHITECTURE, the work log.*

## 완료 조건 / Done when

Linux x64에서 GCC와 Clang으로 `-Werror` 빌드와 두 테스트가 통과하고, 브랜치를 push했을 때 다섯 CI 작업이 통과하며 `rex86_probe`의 `version`, `page_size`, `cpu_state_bytes`, `engine`, `run` 값이 다섯 곳에서 같다(`pointer_bytes`만 호스트에 따라 4 또는 8이다).

*GCC and Clang `-Werror` builds and both tests pass on Linux x64, the five CI jobs pass on a branch push, and `rex86_probe` prints the same `version`, `page_size`, `cpu_state_bytes`, `engine` and `run` on all five hosts (only `pointer_bytes` differs, 4 or 8 by host).*
