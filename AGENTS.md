# AGENTS.md

이 문서는 [rePIU의 AGENTS.md](https://github.com/nworkers/rePIU/blob/main/AGENTS.md)에서 CPU 코어 저장소에 필요한 규칙만 뽑아 정리한 것이다. rePIU의 규칙 가운데 게스트 형식(DOS/4GW), HLE 경계, 자산, 플랫폼 디렉터리에 관한 항목은 이 저장소에 해당하지 않으므로 빼고, 그 자리에 코어의 경계 규칙과 소비자 규칙을 두었다.

*This document is the subset of [rePIU's AGENTS.md](https://github.com/nworkers/rePIU/blob/main/AGENTS.md) that applies to a CPU core repository. rePIU's rules about the guest format (DOS/4GW), HLE boundaries, assets and platform directories do not apply here and are replaced by the core's boundary rules and the consumer rules.*

## 프로젝트 개요

rex86은 [rePIU](https://github.com/nworkers/rePIU)와 [re2DJ](https://github.com/nworkers/re2DJ)가 호스트 CPU가 x86이 아닌 환경(브라우저의 WebAssembly, ARM 기기)에서 원본 32비트 x86 코드를 실행하기 위해 함께 쓰는 IA-32 사용자 모드 CPU 코어 라이브러리다.

코어는 CPU다. 레지스터 상태, 게스트 메모리 뷰, 디코더, 실행 엔진(인터프리터와 번역 백엔드), 검증 하네스까지가 범위다. 게스트 실행 형식(LE, PE32), 운영체제(DOS, Win32), 그래픽 API, 자산은 코어가 모르며, 소비자 프로젝트가 호스트 계약(`Environment`)을 구현해 공급한다.

목표는 원본 게임 로직을 재구현하는 것이 아니라, 원본 x86 코드가 실행 주체로 남을 수 있는 CPU를 x86이 아닌 호스트에 제공하는 것이다.

모든 구현 결정은 이 원칙을 따른다.

## Project Overview

rex86 is the IA-32 user-mode CPU core library that [rePIU](https://github.com/nworkers/rePIU) and [re2DJ](https://github.com/nworkers/re2DJ) share to run original 32-bit x86 code on hosts whose CPU is not x86: WebAssembly in a browser, and ARM devices.

The core is a CPU. Its scope is the register state, the guest memory view, the decoder, the execution engines (an interpreter and translation backends) and the verification harness. The guest executable format (LE, PE32), the operating system (DOS, Win32), the graphics API and the assets are unknown to the core; the consumer projects supply them by implementing the host contract (`Environment`).

The goal is not to reimplement original game logic but to give hosts that are not x86 a CPU on which the original x86 code stays the executing subject.

All implementation decisions follow this principle.

---

## 핵심 설계 목표

우선순위:

1. 게스트 명령의 의미를 Intel SDM대로 보존한다.
2. 같은 입력에 모든 호스트(x86, x86-64, wasm32, AArch64)에서 비트 단위로 같은 결과를 낸다.
3. 코어는 게스트 형식, OS, 그래픽, 자산을 모른다. 호스트가 필요한 것은 전부 콜백으로 받는다.
4. 인터프리터가 정확성 기준이고, 번역 백엔드는 인터프리터와 같은 결과를 내야 한다.
5. QEMU, Bochs, Box86 같은 전염성 라이선스 구현의 소스를 통합하지 않는다.
6. 두 소비자 프로젝트에 모두 맞는 공용 구조를 먼저 설계하고, 한쪽에만 필요한 것은 기능 플래그나 콜백 기본값으로 둔다.

## Primary Design Goals

Priority order:

1. Preserve the meaning of guest instructions as the Intel SDM defines it.
2. Produce bit-identical results for the same input on every host (x86, x86-64, wasm32, AArch64).
3. The core knows no guest format, OS, graphics or assets; everything it needs from the host arrives through callbacks.
4. The interpreter is the correctness reference, and a translation backend must produce the interpreter's results.
5. Do not integrate source from copyleft implementations such as QEMU, Bochs or Box86.
6. Design shared structures that fit both consumer projects first; anything one of them alone needs is a feature flag or a callback default.

---

## 참조 문서

아키텍처 변경 전에는 관련 문서를 먼저 읽고 갱신한다.

* `docs/PROJECT_CHARTER.md`
* `ARCHITECTURE.md`
* `docs/CODING_STYLE.md`
* `docs/design/`
* `docs/work-orders/`
* `docs/work-logs/`
* `docs/analysis/`
* `docs/kb/`

## Reference Documents

Read and update the relevant documents before making architectural changes.

* `docs/PROJECT_CHARTER.md`
* `ARCHITECTURE.md`
* `docs/CODING_STYLE.md`
* `docs/design/`
* `docs/work-orders/`
* `docs/work-logs/`
* `docs/analysis/`
* `docs/kb/`

---

## 핵심 원칙

1. 사용자 요구사항을 받으면 바로 구현하지 말고 먼저 설계를 작성하거나 기존 설계를 갱신한다.
2. 설계가 정리되면 구현 계획서를 작성하거나 갱신한 뒤 구현을 시작한다.
3. 코드 변경이 있는 모든 작업은 작업 지시 문서와 작업 로그 문서를 남긴다.
4. 문서는 기본적으로 한국어를 먼저 쓰고 바로 아래에 영어 번역을 추가한다. 이 규칙은 `docs/` 아래 문서와 `README.md`, `ARCHITECTURE.md` 같은 저장소 문서에만 적용되며, 소스 코드 주석에는 적용하지 않는다.
5. 기존 코드와 문서의 의도를 확인한 뒤 수정하며, 큰 구조 변경은 근거를 문서에 남긴다.
6. 모든 호스트에 공용인 구조를 우선 설계하고, 호스트별 세부 사항은 분리한다.
7. 코어의 공개 계약을 바꾸는 작업은 두 소비자 프로젝트에 미치는 영향을 설계에 함께 적는다.

## Core Principles

1. When a user requirement is received, do not implement immediately. First write a design or update the existing design.
2. After the design is clear, write or update the implementation work order before starting implementation.
3. Every task that changes code must leave a work-order document and a work-log document.
4. Documents are written in Korean first, followed immediately by an English translation. This applies to documents under `docs/` and repository documents such as `README.md` and `ARCHITECTURE.md`; it does not apply to source-code comments.
5. Confirm the intent of existing code and documents before modifying them. Document the rationale for large structural changes.
6. Design structures shared by every host first, and keep host-specific details separated.
7. A task that changes the core's public contract records its effect on both consumer projects in the design.

---

## 응답 태도 규칙

* 모든 질문에 대한 답변은 존대말을 사용하고 예의 있는 태도를 유지한다.

## Response Tone Rules

* Use polite Korean speech and maintain a respectful attitude for all answers.

---

## 요구사항 처리 절차

1. 요구사항 접수와 GitHub 이슈 생성(작업 번호는 이슈 번호)
2. 관련 코드, 문서, 소비자 프로젝트 맥락 확인
3. 설계 문서 작성 또는 갱신
4. 구현 계획서 작성 또는 갱신
5. 사용자 승인 또는 현재 진행 맥락 확인
6. 구현
7. 빌드 또는 범위에 맞는 검증
8. 작업 로그 작성

단, 요구사항이 단순 질문이거나 확인 요청이라면 해당 내용에 바로 응답하고 문서화 및 코드 구현은 하지 않는다.

## Requirement Handling Procedure

1. Receive the requirement and create its GitHub issue (the issue number is the task number).
2. Inspect the relevant code, documentation and consumer-project context.
3. Write or update the design document.
4. Write or update the implementation work order.
5. Confirm user approval or the current active context.
6. Implement.
7. Run a build or verification appropriate to the scope.
8. Write the work log.

If the requirement is a simple question or confirmation request, answer it directly without documentation or code implementation.

---

## 문서 작성 규칙

* 설계 문서는 `docs/design/` 아래에 둔다.
* 계획 문서는 `docs/work-orders/` 아래에 둔다.
* 작업 결과와 회고는 `docs/work-logs/` 아래에 둔다.
* 사용자가 직접 수행하는 검증, 측정, 운영 절차는 `docs/guides/` 아래에 둔다. 특정 작업의 일회성 증거가 아니라 반복 수행 가능한 절차만 두고, 근거가 되는 작업 로그와 설계를 링크한다.
* 프로젝트의 큰 방향성은 `docs/PROJECT_CHARTER.md`에 반영한다.
* 현재 구현되는 코드의 설계와 구조는 `ARCHITECTURE.md`에 지속적으로 반영한다.
* 규칙이 바뀌면 `AGENTS.md`와 관련 문서를 함께 갱신한다.
* 작업 단위 문서 파일명은 `YYYYMMDD-iNNN-slug.md` 형식을 사용한다. `NNN`은 작업 이슈 번호를 세 자리 이상으로 채운 값이다(예: 이슈 #1은 `20261007-i001-slug.md`).
* Markdown 문서에서 구조, 관계, 흐름, 호출 순서, 상태 전이, 주소 변환을 도식화할 수 있으면 이해를 돕기 위해 Mermaid를 적극적으로 사용한다.
* 세 개 이상의 구성요소, 단계, 분기, 계층이 있으면 Mermaid 적용 가능성을 검토하고, flowchart, sequenceDiagram, stateDiagram, classDiagram 등 내용에 맞는 형식을 선택한다.
* 도식이 본문 이해를 실질적으로 개선하는 경우 최대한 포함하되, 단순 사실 하나나 한 단계 설명에는 불필요한 도식을 강제하지 않는다.
* Mermaid 도식은 본문과 같은 사실 및 확인 상태를 표현해야 하며, 본문 설명을 완전히 대체하지 않는다.

## Documentation Rules

* Put design documents under `docs/design/`.
* Put work-order documents under `docs/work-orders/`.
* Put work results and retrospectives under `docs/work-logs/`.
* Put procedures the user runs themselves (verification, measurement, operations) under `docs/guides/`. Keep only repeatable procedures there rather than one-off evidence, and link the design and work log they rest on.
* Reflect the project's broad direction in `docs/PROJECT_CHARTER.md`.
* Continuously reflect the design and structure of currently implemented code in `ARCHITECTURE.md`.
* When rules change, update `AGENTS.md` and the related documents together.
* Name task documents `YYYYMMDD-iNNN-slug.md`, where `NNN` is the task's issue number padded to at least three digits (for example, issue #1 is `20261007-i001-slug.md`).
* Actively use Mermaid in Markdown documents whenever structure, relationships, flows, call sequences, state transitions or address translation can be visualized to improve understanding.
* Evaluate Mermaid whenever content has three or more components, steps, branches or layers, choosing an appropriate form such as flowchart, sequenceDiagram, stateDiagram or classDiagram.
* Include useful diagrams as broadly as practical when they materially improve the prose, but do not force them into single-fact or one-step explanations.
* Mermaid diagrams must express the same facts and confirmation status as the prose and must not completely replace the written explanation.

---

## 분석 및 지식 기반 유지 규칙

* 호스트 CPU 대조, 사양 확인, 소비자 프로젝트의 게스트 census로 새로운 사실이 확인되거나 기존 결론이 바뀌면 같은 작업에서 관련 `docs/analysis/` 문서를 갱신한다. `docs/analysis/`는 이 프로젝트가 직접 확인한 사실(명령 의미의 측정 결과, 호스트별 차이, 게스트가 실제로 쓰는 명령 집합)을 주제별로 둔다.
* 작업 중 프로젝트 이해에 중요한 새로운 용어, 표준, 명령 인코딩, CPU 동작, 호스트 ISA의 특성이 등장하면 관련 `docs/kb/` 문서를 작성하거나 갱신한다.
* 외부 자료에서 얻은 기술 지식에는 가능한 한 Intel SDM, Arm Architecture Reference Manual, WebAssembly 사양, 공식 프로젝트 문서 같은 권위 있는 출처 링크를 가까운 위치에 남긴다.
* `docs/analysis/`는 확인됨, 추정, 미확정 내용을 반드시 구분해 표기한다. 호스트 CPU나 사양으로 확인하지 않은 내용을 확정된 사실처럼 기술하지 않는다.
* analysis 또는 kb 파일을 추가하거나 이름을 바꾸면 해당 디렉터리의 `README.md` 링크 색인을 같은 작업에서 갱신한다.
* `docs/work-logs/`는 시간순 작업 증거로 유지하고, `docs/analysis/`와 `docs/kb/`는 주제별 누적 문서로 유지한다.
* 요구사항 맥락을 확인한 뒤 설계 전에 관련 analysis와 kb 문서를 함께 확인하고, 구현과 검증 후 새로 얻은 내용을 반영한다.

## Analysis and Knowledge Base Maintenance Rules

* When a host-CPU comparison, a specification check or a consumer project's guest census confirms a new fact or changes an earlier conclusion, update the relevant `docs/analysis/` topic in the same task. `docs/analysis/` holds what this project verified itself (measured instruction semantics, differences between hosts, the instruction set a guest actually uses), organized by topic.
* When important new terminology, standards, instruction encodings, CPU behavior or host-ISA properties arise, create or update the relevant `docs/kb/` topic.
* Place authoritative external links near derived technical knowledge, preferring the Intel SDM, the Arm Architecture Reference Manual, the WebAssembly specification and official project documentation.
* Always mark statements in `docs/analysis/` as confirmed, inferred or unresolved. Never state something as confirmed unless it was verified against a host CPU or the specification.
* When adding or renaming an analysis or knowledge file, update the linked index in that directory's `README.md` in the same task.
* Keep `docs/work-logs/` as chronological evidence, while maintaining `docs/analysis/` and `docs/kb/` as cumulative topic references.
* After inspecting requirement context, review relevant analysis and knowledge topics before design, then incorporate new findings after implementation and verification.

---

## 구현 규칙

* 코어 라이브러리(`include/rex86/`, `src/`)는 호스트 OS 헤더를 포함하지 않는다. `<windows.h>`, `<unistd.h>`, `<sys/mman.h>`, `<emscripten.h>`는 코어에 등장하지 않는다. 코어가 OS에서 필요한 것(코드 캐시 메모리, 시계)은 소비자가 구현하는 콜백 인터페이스로 받는다.
* 호스트 OS나 호스트 CPU에 의존하는 코드는 `tests/host/<os>/`(호스트 CPU 대조 fuzz)와 `src/tools/<도구>/`의 호스트 전용 파일에만 둔다. 그런 파일은 첫머리에서 `#error`로 자기 호스트를 확인한다.
* 실행 엔진은 디렉터리로 나눈다. `src/interp/`(인터프리터), `src/translate/ir/`(x86 블록을 IR로 올리는 프런트엔드), `src/translate/wasm/`, `src/translate/aarch64/`(백엔드). x86 명령의 의미는 인터프리터와 IR 프런트엔드에만 있고, 백엔드는 IR만 안다. 백엔드에 x86 의미를 다시 쓰지 않는다.
* 코어는 게스트 형식, OS, 그래픽 API, 자산, 특정 게임을 모른다. 소비자 한쪽의 사정이 코어에 들어와야 하면 기능 플래그(`Features`)나 콜백의 기본값으로 표현하고, 그 사정이 무엇인지 설계에 적는다.
* 성립하지 않는 기능은 조용히 성공하는 더미로 두지 않는다. 거짓이나 명시적 상태(`kNoEngine` 같은 열거값)를 돌려주고 이유를 한 번 보고한다.
* 새 기능을 추가할 때는 테스트 전략 또는 최소 검증 절차를 문서에 함께 남긴다. 명령 의미를 추가하는 작업은 단위 테스트와 호스트 CPU 대조를 같은 작업에서 둔다.
* 코드 수정이 있는 작업은 영향 범위에 맞는 빌드 검증을 수행하고, 불가능하면 이유를 작업 로그에 남긴다.
* 구현과 검증은 같은 작업 단위에서 끝낸다. 검증만 후속 작업으로 넘기지 않는다.
* 코딩 스타일 세부 규칙은 `docs/CODING_STYLE.md`에서 관리한다.
* 소스 코드 주석은 영어로만 작성한다. 한국어 주석과 한국어, 영어 이중 언어 주석을 남기지 않으며, 기존 파일을 수정하다 발견하면 같은 작업에서 영어로 정리한다. 세부 규칙은 `docs/CODING_STYLE.md`의 주석 언어 항목을 따른다.
* 큰 기능은 하나의 거대 파일에 누적하지 않는다. 상태, 디코드, 의미, 백엔드처럼 책임이 다른 부분을 별도 header/source로 분리한다.
* 기존 파일에 기능을 추가할 때 독립적으로 이름 붙일 수 있는 하위 시스템이면 전용 파일로 추출하고, 통합 지점에는 orchestration과 adapter 코드만 남긴다.
* 프로젝트 기본 라이선스는 `BSD 3-Clause License`를 기준으로 한다.
* 서드파티 오픈소스 도입 시 GPL, LGPL, AGPL 같은 전염성 라이선스는 사용하지 않는다. QEMU, Bochs, Box86, Box64, MAME, Unicorn은 이 규칙에 따라 코드 수준으로 참고하지 않는다.
* 서드파티 라이선스는 도입 전에 확인하고 `THIRD_PARTY_NOTICES.md`에 이름, 버전, 라이선스, 경로를 같은 작업에서 기록한다. 예외가 필요하면 먼저 문서화와 사용자 확인을 거친다.

## Implementation Rules

* The core library (`include/rex86/`, `src/`) includes no host OS header. `<windows.h>`, `<unistd.h>`, `<sys/mman.h>` and `<emscripten.h>` do not appear in the core. What the core needs from the OS (code-cache memory, clocks) arrives through callback interfaces the consumer implements.
* Code that depends on the host OS or the host CPU lives only in `tests/host/<os>/` (the host-CPU comparison fuzz) and in host-specific files under `src/tools/<tool>/`. Such a file checks its own host with `#error` at the top.
* Execution engines are split by directory: `src/interp/` (the interpreter), `src/translate/ir/` (the frontend lifting x86 blocks to IR), `src/translate/wasm/` and `src/translate/aarch64/` (backends). The meaning of x86 instructions exists only in the interpreter and the IR frontend; a backend knows IR alone. Never reimplement x86 semantics in a backend.
* The core knows no guest format, OS, graphics API, asset or particular game. When one consumer's circumstance must enter the core, express it as a feature flag (`Features`) or a callback default, and record in the design what that circumstance is.
* A capability that does not hold is never a quietly succeeding dummy. Return false or an explicit state (an enumerator such as `kNoEngine`) and report the reason once.
* When adding a feature, document the test strategy or minimum verification procedure. A task that adds instruction semantics adds its unit tests and host-CPU comparison in the same task.
* For tasks that modify code, run build verification appropriate to the impact. If verification is impossible, record the reason in the work log.
* Finish implementation and verification in the same task unit. Do not defer verification alone to a follow-up task.
* Maintain detailed coding style rules in `docs/CODING_STYLE.md`.
* Write source-code comments in English only. Leave no Korean or bilingual comments, and convert any found while editing an existing file in the same task. The detailed rule is the comment-language section of `docs/CODING_STYLE.md`.
* Do not accumulate major features in one monolithic file. Separate parts with different responsibilities, such as state, decoding, semantics and backends, into dedicated headers and sources.
* When adding to an existing file, extract any independently named subsystem and leave only orchestration or adapter code at the integration point.
* Use the `BSD 3-Clause License` as the default project license baseline.
* Do not introduce third-party open source with copyleft licenses such as GPL, LGPL or AGPL. Under this rule QEMU, Bochs, Box86, Box64, MAME and Unicorn are not consulted at the code level.
* Check third-party licenses before adoption and record the name, version, license and path in `THIRD_PARTY_NOTICES.md` in the same task. If an exception is needed, document it and confirm with the user first.

---

## 소비자 규칙

* 소비자는 rePIU와 re2DJ다. 두 저장소는 CMake FetchContent로 이 저장소의 release tag를 고정해 `rex86::core`를 링크한다. 로컬 동시 개발은 `FETCHCONTENT_SOURCE_DIR_REX86`으로 형제 체크아웃을 가리킨다.
* 공개 계약(`include/rex86/`)을 바꾸는 작업은 설계에 두 소비자의 어댑터에 미치는 영향을 적고, 머지 뒤 각 소비자 저장소에 tag를 올리는 작업을 만든다.
* 소비자의 게스트에서 확인된 명령 집합(census)은 소비자 저장소가 측정하고, 그 결과를 이 저장소의 `docs/analysis/`에 소비자와 측정 시점을 밝혀 기록한다.
* 소비자 한쪽에서만 재현되는 문제는 먼저 이 저장소의 단위 테스트나 trace로 재현한 뒤 고친다. 소비자 코드로만 재현되는 수정은 하지 않는다.

## Consumer Rules

* The consumers are rePIU and re2DJ. Both link `rex86::core` through CMake FetchContent pinned to a release tag of this repository; local co-development points `FETCHCONTENT_SOURCE_DIR_REX86` at a sibling checkout.
* A task that changes the public contract (`include/rex86/`) records its effect on both consumers' adapters in the design and, after the merge, creates the tag-bump task in each consumer repository.
* The instruction set confirmed in a consumer's guest (the census) is measured by the consumer repository, and the result is recorded in this repository's `docs/analysis/` naming the consumer and the time of measurement.
* A problem reproduced in one consumer alone is first reproduced here as a unit test or a trace, then fixed. No fix is made that only consumer code can reproduce.

---

## 작업 단위 규칙과 GitHub issue 연동

* task는 GitHub issue로 만든다. task 번호는 GitHub issue 번호를 쓰고, 문서와 대화에서 `#N`으로 부른다.
* 요구사항을 접수하면 설계를 쓰기 전에 `gh issue create`(또는 GitHub 웹)로 이슈를 만든다. 제목은 작업을 한 줄로 적고, 본문에는 요구사항 요약과 (작성된 뒤) 설계 문서, 작업 지시서, 작업 로그의 링크를 둔다.
* 설계 문서, 작업 지시서, 작업 로그의 첫머리에 `이슈: [#N](https://github.com/reexec/rex86/issues/N)`을 적고, 세 문서가 서로 링크한다.
* 기존 task의 후속 작업(남은 검증, 후속 수정)은 새 issue를 만들지 않고 그 task의 issue에 코멘트로 붙이며, 작업 로그에는 날짜별 절을 더한다.
* 커밋 메시지는 제목과 본문 모두 영어로만 작성한다.
* 커밋 제목 끝에 관련 issue 번호를 `(#1)`처럼 붙인다. 이슈를 닫는 커밋이나 PR 본문에는 `Closes #1`을 적는다.
* 의미 있는 작업마다 하나의 작업 지시 문서를 만들고, 작업이 끝나면 대응되는 작업 로그를 남긴다.
* 설계 없이 바로 코드만 추가하지 않는다.
* 초기 구조 작업이라도 디렉터리 목적과 향후 확장 방향을 문서로 남긴다.
* 단순 질문이나 확인 요청은 이슈를 만들지 않는다.

## Task Unit Rules and GitHub Issue Integration

* A task is created as a GitHub issue. The task number is the GitHub issue number, written `#N` in documents and conversation.
* When a requirement is received, create its issue with `gh issue create` (or the GitHub web UI) before writing the design. The title states the task in one line; the body holds a summary of the requirement and, once written, links to the design, the work order and the work log.
* The design, the work order and the work log open with `이슈: [#N](https://github.com/reexec/rex86/issues/N)` and link to one another.
* Follow-up work on an existing task (verification left open, a follow-up fix) goes to that task's issue as a comment rather than a new issue, and the work log gains a dated section.
* Write commit messages, both title and body, in English only.
* End commit titles with the related issue number, as in `(#1)`. A commit or PR body that closes the issue says `Closes #1`.
* Create one work-order document for each meaningful task, and leave the corresponding work log when the task is complete.
* Do not add code directly without a design.
* Even for initial structure work, document the directory purpose and future extension direction.
* Simple questions and confirmation requests get no issue.

---

## 브랜치와 머지 규칙

* 사용자가 작업을 요청하면 먼저 현재 Git 브랜치명을 확인한다.
* 현재 브랜치가 `main`이면 작업용 브랜치를 새로 만든 뒤 작업한다. 브랜치 이름은 `work/iNNN-slug`로 하고 `NNN`은 이슈 번호다(예: `work/i001-repository-and-public-contract`).
* 작업 단위가 하나 끝날 때마다 관련 변경을 Git 커밋으로 남긴다. `main`에 직접 커밋하지 않는다.
* CI(`.github/workflows/ci.yml`)는 모든 브랜치 push에서 모든 타깃을 검증한다. 같은 저장소 브랜치에서 연 PR은 push 실행의 결과를 쓰고, fork에서 온 PR만 `pull_request`로 다시 돈다. 같은 브랜치에 새 push가 오면 앞선 실행을 취소하되 `main`은 커밋마다 결과를 남긴다.
* 프로젝트 버전은 저장소 루트의 `VERSION` 파일에서 `major.minor.patch` 형식으로 관리한다. 사용자가 머지를 요청하면 patch를 1 올리고, minor 올림 요청이면 minor를 1 올리고 patch를 0으로, major 올림 요청이면 major를 1 올리고 minor와 patch를 0으로 한다.
* 머지는 사용자가 요청할 때만 하며, 머지 요청이 push와 PR 생성의 승인이다. 순서는 다음과 같다.
  1. `VERSION`을 올리고 릴리스 노트 `docs/release-notes/v<version>.md`를 쓴 뒤 커밋한다.
  2. `main`이 앞서 있으면 브랜치를 `main` 위로 갱신(rebase 또는 merge)해 다시 push한다. CI는 브랜치 head를 검증하기 때문이다.
  3. 작업 브랜치를 push하고, PR이 없으면 만든다. PR 본문에는 해결하는 이슈를 `Closes #N`으로 적는다.
  4. CI 통과를 확인한다. 빨간 CI로는 머지하지 않는다.
  5. 브랜치의 커밋 제목들을 보고 전체 변경을 나타내는 제목으로 squash merge한다(`gh pr merge --squash --delete-branch`). 머지 커밋 제목 끝에도 `(#N)`을 붙인다.
  6. `git checkout main && git pull --ff-only`로 로컬 `main`을 원격과 같게 한다.
  7. squash 커밋에 `VERSION`과 같은 값의 annotated tag를 `vmajor.minor.patch`로 붙인다. tag 메시지에는 핵심 변경을 한 줄로 적는다. tag는 로컬까지만 만들고 원격 push는 사용자가 한다.
  8. 남은 로컬 작업 브랜치를 삭제한다.
* 소비자 저장소의 FetchContent가 가리키는 것이 이 tag이므로, 사용자가 tag를 push한 뒤에 소비자의 tag 올림 작업을 시작한다.
* 릴리스 노트에는 해결된 이슈 `#N`(제목)과 PR 번호를 적는다. squash 커밋 ID는 머지 뒤에야 생기므로 노트 파일에 미리 적지 않고, 다음 릴리스 노트나 GitHub Release 본문에서 보완한다.

## Branch and Merge Rules

* When the user requests work, first check the current Git branch name.
* If the current branch is `main`, create a task branch before making changes. Name it `work/iNNN-slug`, where `NNN` is the issue number (for example `work/i001-repository-and-public-contract`).
* Leave a Git commit for the related changes whenever one task unit is complete. Never commit to `main` directly.
* CI (`.github/workflows/ci.yml`) checks every target on every branch push. A PR from a branch of this repository uses the push run's results; only a PR from a fork runs again on `pull_request`. A newer push to the same branch cancels the earlier run, except on `main`, where every commit keeps its result.
* Manage the project version in the repository-root `VERSION` file using `major.minor.patch`. On a merge request, raise the patch by 1; on a minor-bump request, raise the minor by 1 and reset the patch to 0; on a major-bump request, raise the major by 1 and reset the minor and patch to 0.
* Merge only when the user requests it; the merge request approves the push and the PR. The order is:
  1. Bump `VERSION`, write the release notes `docs/release-notes/v<version>.md`, and commit.
  2. If `main` has moved ahead, update the branch onto `main` (rebase or merge) and push again, because CI checks the branch head.
  3. Push the task branch and open a PR if none exists. The PR body names the resolved issue as `Closes #N`.
  4. Confirm CI passes. Never merge on red CI.
  5. Squash-merge with a title that sums up the branch's commit titles (`gh pr merge --squash --delete-branch`), ending the title with `(#N)` as well.
  6. `git checkout main && git pull --ff-only` so local `main` matches the remote.
  7. Tag the squash commit with an annotated tag matching `VERSION`, `vmajor.minor.patch`, whose message states the key change in one line. Create the tag locally only; the user pushes it.
  8. Delete any remaining local task branch.
* The consumers' FetchContent points at this tag, so the consumers' tag-bump tasks start after the user has pushed the tag.
* Release notes record the resolved issues `#N` (with titles) and the PR number. A squash commit's ID exists only after the merge, so it is not written into the note file in advance; the next release notes or the GitHub Release body supply it.

---

## 아키텍처 규칙

* 코어는 CPU만 제공한다. 로더, HLE, OS, 그래픽, 입력, 자산은 소비자의 것이다.
* 호스트 계약은 이벤트와 재개다. 코어는 게이트 주소 도달, 소프트웨어 인터럽트, 포트 I/O, 폴트, 예산 소진에서 멈추고 32비트 게스트 값만 담긴 이벤트를 돌려주며, 호스트가 상태를 고친 뒤 재개한다. 이벤트에 호스트 포인터나 백엔드 내부 핸들을 넣지 않는다.
* 인터프리터는 IR을 거치지 않는 정확성 기준이다. 번역 백엔드의 결과는 인터프리터와 같아야 하고, 다르면 백엔드가 틀린 것으로 본다.
* 결과는 호스트에 무관하게 비트 단위로 같아야 한다. x87은 80비트 소프트웨어 구현으로 둔다. 호스트 FPU를 쓰는 빠른 경로는 같은 결과를 내는 것이 증명된 뒤에만 더한다.
* 하드웨어 페이지 보호, 하드웨어 폴트 전달, 호스트 스레드 정지에 기대지 않는다. 자기 수정 코드 감지는 페이지 속성표와 번역 시점 검사로 한다. 그래야 wasm과 JIT 금지 호스트에서도 같은 코드가 돈다.
* 엔진 선택(인터프리터만, 또는 번역 백엔드 포함)은 빌드 시점이 아니라 실행 시점에 한다. 호스트가 코드 캐시 서비스를 주지 않으면 코어는 인터프리터로만 동작한다.
* 모든 하위 시스템은 독립적으로 교체 가능해야 한다. 디코더, 인터프리터, IR, 각 백엔드는 공개 계약을 바꾸지 않고 바꿀 수 있어야 한다.

## Architecture Rules

* The core provides the CPU alone. Loaders, HLE, the OS, graphics, input and assets belong to the consumers.
* The host contract is events and resumption. The core stops at a gate address, a software interrupt, port I/O, a fault or an exhausted budget, returns an event holding 32-bit guest values only, and resumes after the host has edited the state. An event never carries a host pointer or a backend's internal handle.
* The interpreter is the correctness reference and does not go through the IR. A translation backend's results must equal the interpreter's; where they differ, the backend is wrong.
* Results are bit-identical regardless of host. x87 is an 80-bit software implementation; a fast path using the host FPU is added only once it is proven to produce the same results.
* Never rely on hardware page protection, hardware fault delivery or stopping host threads. Self-modifying code is detected through the page attribute table and checks placed at translation time, so the same code runs on wasm and on hosts that forbid JIT.
* The choice of engine (interpreter only, or with a translation backend) is made at run time, not build time. Without code-cache services from the host, the core runs on the interpreter alone.
* Every subsystem is independently replaceable. The decoder, the interpreter, the IR and each backend can change without changing the public contract.

---

## 개발 철학

여러 구현이 가능할 때는 Intel SDM의 의미를 보존하면서 두 소비자 프로젝트의 통합 부담을 최소화하는 방법을 선택한다.

최적화보다 정확성을 우선한다. 측정 없이 최적화하지 않는다.

## Development Philosophy

When multiple implementations are possible, choose the one that preserves the Intel SDM's meaning while minimizing the integration burden on the two consumer projects.

Accuracy is preferred over optimization. Nothing is optimized without a measurement.

---

## 법적 범위

이 저장소는 원본 게임 바이너리나 데이터를 포함하지 않으며 배포하지도 않는다. 테스트는 실행 중에 만든 합성 코드로 수행한다.

## Legal Scope

This repository neither contains nor distributes original game binaries or data. Tests run on synthetic code generated at run time.
