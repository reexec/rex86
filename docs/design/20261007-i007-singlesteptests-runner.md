# #7 설계 : SingleStepTests/80386 기반 독립 검증 러너 / #7 design : the SingleStepTests/80386 independent validation runner

이슈: [#7](https://github.com/reexec/rex86/issues/7) | 지시서: [20261007-i007](../work-orders/20261007-i007-singlesteptests-runner.md) | 로그: [20261007-i007](../work-logs/20261007-i007-singlesteptests-runner.md)

## 배경 / Background

사용자는 코어 검증을 rePIU와 re2DJ에 의존하지 않는 독립 공개 샘플로 수행하기를 요구했다. 조사 결과는 다음과 같다.

| 후보 | 라이선스 | 평가 |
|---|---|---|
| [SingleStepTests/80386](https://github.com/SingleStepTests/80386) | MIT | **채택**. 실제 Intel 386EX에서 생성한 명령 단위 테스트(opcode당 100~2,500개, 초기/최종 상태와 undefined 마스크). 하드웨어 유래 기준을 **x86이 아닌 호스트의 CI에서도** 돌릴 수 있다 |
| [test386.asm](https://github.com/barotto/test386.asm) | GPLv3 | **공식 경로 제외**. 전염성 라이선스 금지 규칙상 vendoring 불가. 사용자가 로컬에서 돌리는 선택적 교차 확인으로만 |
| DOOM 1.9 shareware (`DOOM1` 실행 파일) | 재배포 가능 shareware | census와 실행 검증의 실전급 게스트 후보. 저장소에 넣지 않고 절차만 문서화. 후속 작업 |
| CoreMark | Apache-2.0 | 성능 벤치마크(README 목표 3) 워크로드 후보. 벤치마크 하네스 작업에서 |

이 작업은 SingleStepTests를 채택한다. 테스트는 [MOO v1.1 형식](https://github.com/dbalsom/moo/blob/main/doc/moo_format_v1.md)(chunked 바이너리, little-endian)으로 배포되고, real mode 테스트가 완성 상태다(protected mode는 업스트림 미완, FPU 미포함).

*The user requires validation independent of rePIU and re2DJ. Of the candidates surveyed, SingleStepTests/80386 (MIT) is adopted: hardware-generated per-instruction tests (100–2,500 per opcode with initial/final state and undefined masks) whose hardware-derived truth runs on non-x86 CI hosts too. test386.asm is excluded from the official path (GPLv3; optional local cross-check only), the DOOM 1.9 shareware executable is a follow-up census/execution guest documented but never committed, and CoreMark (Apache-2.0) is the benchmark-workload candidate for the harness task. The tests ship in the MOO v1.1 chunked little-endian format; real-mode tests are complete upstream (protected mode pending, no FPU).*

## 결정 1: MOO 파서는 사양 기반 자체 구현이다 / Decision 1: the MOO parser is written from the specification

`src/tools/sst/moo_reader.{h,cpp}`. [MOO v1.1 사양](https://github.com/dbalsom/moo/blob/main/doc/moo_format_v1.md)을 보고 직접 작성하며, 업스트림의 C++ 파서(MIT)는 복사하지 않는다. 사양이 완결적이고 파서가 작으므로(청크 걷기 + RG32/RM32/RAM/BYTS 해석) 자체 구현이 의존성과 코딩 스타일 모두에서 깨끗하다.

* 읽는 청크: `MOO `(헤더, 테스트 수, CPU ID), `META`(opcode, mnemonic, cpu_mode), `TEST`(`NAME`, `BYTS`, `INIT`/`FINA`의 `RG32`/`RM32`/`RAM `, `EXCP`, `HASH`). 모르는 청크는 사양대로 길이 필드로 건너뛴다. `CYCL`(사이클 데이터)과 `QUEU`는 이 코어의 검증에 쓰지 않으므로 건너뛴다.
* 압축(`.MOO.gz`)은 다루지 않는다. zlib 의존성을 들이는 대신 사용자가 내려받아 `gunzip`한 디렉터리를 러너에 준다.
* 테스트 데이터는 저장소에 넣지 않는다(코드 아닌 외부 데이터, 수백 MB). `THIRD_PARTY_NOTICES.md`에는 코드가 아니므로 항목을 더하지 않고, 가이드에 출처와 라이선스를 적는다.

*`src/tools/sst/moo_reader.{h,cpp}` is written from the MOO v1.1 specification rather than copying upstream's MIT C++ parser: the spec is complete and the parser small (chunk walking plus RG32/RM32/RAM/BYTS), so an own implementation is cleanest for both dependencies and style. It reads `MOO `, `META` and `TEST` with `NAME`, `BYTS`, the `INIT`/`FINA` register, mask and RAM subchunks, `EXCP` and `HASH`, skipping unknown chunks by their length field as the spec requires, and skipping `CYCL` and `QUEU` which this core's validation does not use. Compressed `.MOO.gz` is not handled — the user hands the runner a `gunzip`ed directory instead of the repo gaining zlib. Test data never enters the repository (external data, hundreds of MB); the guide records source and license, and `THIRD_PARTY_NOTICES.md` gains no entry since no code is introduced.*

## 결정 2: 디코더에 16비트 모드를 더한다 / Decision 2: the decoder gains a 16-bit mode

SingleStepTests의 완성 구간은 real mode라서 기본 피연산자 크기가 16비트다. `Decoder`에 `Mode { kLegacy32, kLegacy16 }` 생성자 인자를 더하고 기본값은 `kLegacy32`로 둔다. #5 설계가 "16비트는 인터프리터 작업에서 모드 인자로 넓힌다"고 적어 둔 바로 그 확장이며, `Features::segments_16bit`(16비트 코드 세그먼트) 지원의 디코드 쪽 선행 작업이기도 하다. 공개 계약은 여전히 무변경이다.

*The suite's complete section is real mode, whose default operand size is 16 bits, so `Decoder` gains a `Mode { kLegacy32, kLegacy16 }` constructor argument defaulting to `kLegacy32` — exactly the widening design #5 deferred, and the decode-side groundwork for `Features::segments_16bit`. The public contract stays unchanged.*

## 결정 3: 러너는 단계적으로 자란다 / Decision 3: the runner grows in stages

`src/tools/sst/main.cpp` → `rex86_sst <dir-or-file> [--verbose]`. Emscripten에서는 빌드하지 않는다(호스트 파일시스템을 읽는 도구).

* **1차(이 작업): 디코더 검증.** 각 테스트의 `BYTS`를 16비트 모드로 디코드해 (a) 디코드 성공, (b) 디코드 길이 == `BYTS` 길이를 검사한다. opcode당 수백 개, 스위트 전체 수십만 개의 하드웨어 검증 인코딩이 디코더를 지나간다. mnemonic 비교(Zydis 대 `META`)는 이름 체계가 달라 정보성 보고로만 둔다. 실패가 있으면 종료 코드 1.
* **2차(인터프리터 작업): 실행 검증.** 같은 러너가 `INIT`의 레지스터와 RAM을 `CpuState`/`GuestMemory`에 적용하고 한 명령을 실행한 뒤 `FINA`와 비교한다. undefined 상태는 `RM32` 마스크로 제외한다. real mode 세그먼트는 `CpuState`의 세그먼트 base 캐시에 `selector * 16`을 넣는 것으로 표현되므로 코어 수정 없이 성립한다. `EXCP`가 있는 테스트는 코어의 `kFault` 이벤트와 대조한다.
* CTest 연동은 configure 시점에 `REX86_SST_DIR`이 주어졌을 때만 테스트를 등록한다. CI는 데이터 크기(압축 ~170MB) 때문에 매 push에서 받지 않고, 도입 여부는 인터프리터 작업에서 캐시 전략과 함께 다시 판단한다.
* MOO 파서의 단위 테스트는 네트워크 없이, 테스트 코드가 사양대로 합성한 MOO 바이트열로 수행한다.

```mermaid
flowchart LR
    DL["사용자: 테스트 다운로드 + gunzip"] --> R["rex86_sst"]
    R --> P["moo_reader: MOO v1.1 파싱"]
    P --> S1["1차: 디코더 길이/성공 검증"]
    P -.-> S2["2차(후속): INIT 적용 → 실행 → FINA 비교 (RM32 마스크)"]
```

*`rex86_sst <dir-or-file>` is not built under Emscripten. Stage 1 (this task) validates the decoder: each test's `BYTS` is decoded in 16-bit mode, checking decode success and length equality — hundreds of hardware-validated encodings per opcode; the Zydis-versus-`META` mnemonic comparison stays informational since the naming schemes differ; any failure exits 1. Stage 2 (the interpreter task) applies `INIT` registers and RAM to `CpuState`/`GuestMemory`, executes one instruction and compares `FINA` under the `RM32` undefined masks; real-mode segmentation is `selector * 16` in the segment base caches, so no core change is needed, and `EXCP` tests compare against the core's `kFault` events. CTest registers a test only when `REX86_SST_DIR` is given at configure time; CI does not download the ~170MB suite on every push, and its adoption is revisited with a caching strategy in the interpreter task. The MOO parser's unit tests run without the network, on MOO bytes the test code synthesizes per the spec.*

## 미확정과 위험 / Unresolved and risks

| 항목 | 상태 | 처리 |
|---|---|---|
| real mode 테스트가 32비트 사용자 모드 코어의 검증으로 충분한가 | **부분적**. ALU/플래그/주소 계산 의미는 모드 무관, 0x66/0x67 prefix로 32비트 연산 포함. 세그먼트 로드 의미는 다름 | 업스트림 protected mode 테스트가 나오면 같은 러너로 수용. 32비트 평탄 코드의 의미는 기존 호스트 CPU 대조 fuzz(1단계 후속)가 마저 덮는다 |
| FPU 미포함 | **확인됨** | x87은 계획대로 호스트 CPU 대조(GCC 80비트 `long double`)와 SoftFloat 검증으로 |
| `BYTS`가 테스트 대상 명령만 담는가(주입된 HLT 제외) | 사양의 "current instruction being tested"로 **추정** | 1차 러너 실행에서 실측으로 확인하고 로그에 기록 |
| 업스트림 revocation(`revocation_list.txt`) | 존재 확인 | 가이드에 다운로드 시 확인하도록 적는다 |

*Risks: real-mode tests validate ALU/flag/address semantics (mode-independent, with 32-bit operations via prefixes) but not 32-bit segment-load semantics — upstream protected-mode tests will slot into the same runner, and the host-CPU comparison fuzz still covers flat 32-bit code; no FPU, as confirmed, so x87 keeps its planned SoftFloat/host-comparison path; whether `BYTS` holds only the instruction under test (not the injected HLT) is inferred from the spec and confirmed empirically in this task's first run; the upstream revocation list is noted in the guide.*

## 검증 / Verification

* 단위 테스트: 합성 MOO 바이트열로 파서(헤더, META, RG32 비트마스크 해석, FINA 부분 레지스터, RAM 항목, 모르는 청크 건너뛰기, 잘린 입력 거부), 16비트 모드 디코더(16/32비트 피연산자 크기 차이 확인).
* 로컬 Windows x86 빌드와 `ctest` 통과. 실제 스위트 일부(소수 opcode 파일)를 내려받아 `rex86_sst` 실행, 결과를 로그에 기록.
* 나머지 호스트는 push 시 CI.

*Unit tests cover the parser on synthesized MOO bytes (header, META, RG32 bitmask decoding, partial FINA registers, RAM entries, unknown-chunk skipping, truncated-input rejection) and the 16-bit decoder mode; the local Windows x86 build and `ctest` pass; a few real suite files are downloaded and run through `rex86_sst` with results recorded in the log; CI checks the remaining hosts on push.*
