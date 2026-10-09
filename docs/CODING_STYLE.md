# 코딩 스타일

rePIU의 `docs/CODING_STYLE.md`를 기준으로, 플랫폼 디렉터리가 없는 CPU 코어 저장소에 맞게 디렉터리 정책만 바꾼 것이다.

*Based on rePIU's `docs/CODING_STYLE.md`, with only the directory policy changed for a CPU core repository that has no platform directories.*

## 기본 원칙

* C++ 코드는 C++20을 기준으로 작성한다. 표준 라이브러리 기능은 CI가 검증하는 가장 오래된 표준 라이브러리에 있는 것만 쓴다. 지금은 Emscripten 3.1.74의 libc++ 18이며, 여기에는 `std::atomic_ref`가 없다(#35).
* 기본 기준은 Google C++ Style Guide를 따른다.
* 프로젝트 예외 규칙은 아래 항목으로 명시한다.
* 게스트 명령의 의미 보존을 우선하며, 편의를 위한 의미 단순화는 하지 않는다.
* 호스트 중립 코어와 호스트 종속 코드(테스트 하네스, 도구)를 분리한다.
* 큰 구조 변경은 설계 문서에 근거를 남긴다.
* 새 기능에는 최소 검증 절차를 함께 문서화한다.
* 독립된 책임을 갖는 큰 기능은 전용 header/source pair로 분리한다.

## Basic Principles

* Write C++ code against C++20, using only the standard-library features of the oldest standard library CI checks: today Emscripten 3.1.74's libc++ 18, which lacks `std::atomic_ref` (#35).
* Follow the Google C++ Style Guide as the baseline.
* Project-specific exceptions are defined below.
* Prioritize preserving the meaning of guest instructions; never simplify semantics for convenience.
* Separate the host-neutral core from host-specific code (test harness, tools).
* Document the rationale for large structural changes in design documents.
* Document a minimum verification procedure with each new feature.
* Give major independently responsible features dedicated header/source pairs.

## 프로젝트 예외 규칙

* 여는 중괄호 `{` 는 같은 줄 끝에 두지 않고 다음 줄에 둔다.
* 탭 문자는 사용하지 않는다.
* 들여쓰기는 공백 4칸을 사용한다.
* 함수명은 Google 스타일에 맞춰 `PascalCase`를 사용한다.
* 변수명은 `snake_case`를 사용한다.
* 클래스 멤버 변수는 `snake_case_`를 사용한다.
* 상수와 enum 값은 `kPascalCase`를 사용한다.
* namespace는 `rex86` 아래에 둔다. 테스트는 `rex86::test`다.
* C++ 소스 파일 확장자는 `.cpp`, 헤더 파일 확장자는 `.h`를 사용한다.
* 헤더 가드는 `REX86_<DIR>_<FILE>_H_` 형식을 사용한다. `#pragma once`는 쓰지 않는다.
* 게스트 값은 `std::uint32_t` 같은 고정 폭 타입만 쓴다. `long`과 `size_t`를 게스트 값에 쓰지 않는다.
* 다중 바이트 게스트 값은 바이트 단위로 조립한다. 호스트의 정렬과 바이트 순서에 기대지 않는다.

## Project-Specific Exceptions

* Place opening braces `{` on the next line instead of at the end of the current line.
* Do not use tab characters.
* Use four spaces for indentation.
* Use `PascalCase` for function names, following Google style.
* Use `snake_case` for variable names.
* Use `snake_case_` for class member variables.
* Use `kPascalCase` for constants and enum values.
* Nest namespaces under `rex86`; tests are `rex86::test`.
* Use `.cpp` for C++ source files and `.h` for headers.
* Use `REX86_<DIR>_<FILE>_H_` header guards. Do not use `#pragma once`.
* Guest values use fixed-width types such as `std::uint32_t` only; never `long` or `size_t`.
* Assemble multi-byte guest values byte by byte; never rely on the host's alignment or byte order.

## 주석 언어

* **소스 코드의 주석은 영어로만 작성한다.** 한국어 주석은 남기지 않는다.
* 한국어와 영어를 함께 적은 이중 언어 주석도 두지 않는다. 같은 내용을 두 벌 유지하면 한쪽만 갱신되어 서로 어긋난다.
* 이 규칙은 **문서 규칙과 다르다.** `docs/` 아래 Markdown 문서는 한국어를 먼저 쓰고 영어 번역을 덧붙이지만(`AGENTS.md`), 소스 주석은 영어 한 벌만 둔다.
* 커밋 메시지, 코드 식별자, 로그 문자열도 영어를 사용한다.
* 적용 대상은 C++ 소스와 헤더, 빌드와 측정 스크립트(`scripts/`, `CMakeLists.txt`, 워크플로 파일)를 포함한 저장소의 모든 코드다. `third_party/` 아래 외부 코드는 원본 그대로 둔다.
* 기존 파일을 수정할 때 한국어 주석을 발견하면 같은 작업에서 영어로 바꾼다.

## Comment Language

* **Write source-code comments in English only.** Do not leave Korean comments.
* Do not keep bilingual comments either: maintaining the same explanation twice lets one copy be updated while the other drifts out of date.
* This rule **differs from the documentation rule**. Markdown under `docs/` leads with Korean and adds an English translation (`AGENTS.md`), while source comments carry one English copy only.
* Commit messages, code identifiers and log strings are English as well.
* The scope is all code in the repository, including C++ sources and headers and the build and measurement scripts (`scripts/`, `CMakeLists.txt`, workflow files). External code under `third_party/` stays as upstream wrote it.
* When editing an existing file that still has Korean comments, convert them in the same task.

## 예시

잘못된 예:

```cpp
int main() {
    if (ready) {
        Run();
    }
}
```

올바른 예:

```cpp
int main()
{
    if (ready)
    {
        Run();
    }
}
```

## Example

Incorrect:

```cpp
int main() {
    if (ready) {
        Run();
    }
}
```

Correct:

```cpp
int main()
{
    if (ready)
    {
        Run();
    }
}
```

## 디렉터리 정책

* 공개 헤더는 `include/rex86/`에 둔다. 한 하위 시스템 안에서만 쓰는 헤더는 해당 `src/` 디렉터리에 둔다.
* 코어 구현은 `src/` 아래에 둔다. 상태와 메모리는 `src/` 루트, 디코더는 `src/decode/`, 인터프리터는 `src/interp/`, 번역 프런트엔드는 `src/translate/ir/`, 백엔드는 `src/translate/wasm/`과 `src/translate/aarch64/`, x87은 `src/fpu/`에 둔다.
* 코어(`include/`, `src/`)는 호스트 OS 헤더를 포함하지 않는다. OS에서 필요한 것은 `include/rex86/environment.h`의 콜백 인터페이스로 받는다.
* 호스트 OS나 호스트 CPU에 의존하는 코드는 `tests/host/<os>/`와 `src/tools/<도구>/`의 호스트 전용 파일에만 두고, 그 파일은 첫머리에서 `#error`로 자기 호스트를 확인한다.
* 백엔드 디렉터리 안의 코드는 그 백엔드의 호스트 ISA(wasm 바이트코드, AArch64 기계어)를 알지만 호스트 OS는 모른다. 코드 캐시의 할당과 보호 전환은 `CodeCacheServices` 콜백이다.
* 비실행 도구는 `src/tools/<도구 이름>/` 아래에 둔다.
* 테스트는 `tests/unit/`(호스트 중립)과 `tests/host/`(호스트 CPU 대조)로 나눈다.

## Directory Policy

* Public headers live in `include/rex86/`. A header used only inside one subsystem stays next to its sources under `src/`.
* The core lives under `src/`: state and memory at the `src/` root, the decoder in `src/decode/`, the interpreter in `src/interp/`, the translation frontend in `src/translate/ir/`, the backends in `src/translate/wasm/` and `src/translate/aarch64/`, x87 in `src/fpu/`.
* The core (`include/`, `src/`) includes no host OS header. What it needs from the OS arrives through the callback interfaces of `include/rex86/environment.h`.
* Code that depends on the host OS or the host CPU lives only in `tests/host/<os>/` and in host-specific files under `src/tools/<tool>/`, each checking its own host with `#error` at the top.
* Code inside a backend directory knows that backend's host ISA (wasm bytecode, AArch64 machine code) but not the host OS; code-cache allocation and protection switching are the `CodeCacheServices` callbacks.
* Non-executing tools live under `src/tools/<tool-name>/`.
* Tests split into `tests/unit/` (host-neutral) and `tests/host/` (host-CPU comparison).

## 라이선스 정책

* 프로젝트 기본 라이선스는 `BSD 3-Clause License`를 기준으로 한다.
* GPL, LGPL, AGPL 등 전염성 라이선스의 서드파티 코드는 도입하지 않는다.
* 서드파티 의존성을 추가하기 전에 라이선스를 확인하고 `THIRD_PARTY_NOTICES.md`에 문서화한다.

## License Policy

* Use the `BSD 3-Clause License` as the project license baseline.
* Do not introduce third-party code under copyleft licenses such as GPL, LGPL or AGPL.
* Check third-party dependency licenses before adding them and record them in `THIRD_PARTY_NOTICES.md`.
