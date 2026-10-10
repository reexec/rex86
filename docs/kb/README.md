# 기술 지식 기반 색인 / Knowledge Base Index

이 디렉터리는 **일반적으로 통용되는** 기술 배경 지식을 주제별로 둔다. IA-32 명령 인코딩과 의미, x87 80비트 형식, WebAssembly와 AArch64의 특성, 호스트 OS의 JIT 제약이 그 예다. 이 프로젝트가 직접 확인한 사실은 [`docs/analysis/`](../analysis/README.md)에 둔다.

*This directory holds **generally applicable** technical background, organized by topic: IA-32 encodings and semantics, the x87 80-bit format, properties of WebAssembly and AArch64, host-OS JIT constraints. Facts this project verified itself live in [`docs/analysis/`](../analysis/README.md).*

외부 자료에서 얻은 내용에는 Intel SDM, Arm Architecture Reference Manual, WebAssembly 사양, 공식 프로젝트 문서 같은 권위 있는 출처 링크를 가까운 위치에 남긴다.

*Place authoritative links (the Intel SDM, the Arm Architecture Reference Manual, the WebAssembly specification, official project documentation) near anything derived from an external source.*

## 문서 / Documents

| 문서 | 내용 |
| --- | --- |
| [target-board-cpus.md](target-board-cpus.md) | 대상 기판(안다미로 MK3, MK5, EZ2DJ 1, 2세대)의 CPU, CPU별 명령 집합과 클럭, 최소 지원 사양의 근거 / the target boards' CPUs (Andamiro MK3, MK5, EZ2DJ generations 1 and 2), their instruction sets and clocks, the basis of the minimum specification |
| [simd.md](simd.md) | MMX와 SSE: 어느 CPU에 무엇이 있나, MMX와 x87의 공유, MXCSR, SIMD 부동소수점 예외와 우선순위, FXSAVE 이미지, 정렬 / which CPU has what, MMX sharing the x87, MXCSR, SIMD floating-point exceptions and their precedence, the FXSAVE image, alignment |
| [x87.md](x87.md) | x87 FPU: 80비트 형식과 분류, 레지스터 스택과 태그, CW/SW, 예외와 우선순위, 환경 이미지 / the 80-bit format and its classes, the register stack and tags, CW/SW, exceptions and their priority, environment images |
| [trap-flag-single-step.md](trap-flag-single-step.md) | 트랩 플래그 단일 스텝으로 사용자 모드에서 명령 하나 관찰하기: 트랩 시점, REP 반복, Linux i386 신호 대응, 대체 스택, 같은 주소 매핑 / observing one instruction from user mode with the trap flag: trap timing, REP iterations, the Linux i386 signal mapping, the alternate stack, same-address mapping |
| [singlesteptests-moo.md](singlesteptests-moo.md) | SingleStepTests 하드웨어 생성 테스트와 MOO v1.1 바이너리 형식 / the hardware-generated SingleStepTests suites and the MOO v1.1 binary format |
| [wasm-runtime-code.md](wasm-runtime-code.md) | WebAssembly에서 실행 중에 만든 코드 부르기: 메모리 import, 함수 포인터인 테이블 인덱스, `addFunction`, 동기와 비동기 컴파일의 한도 / calling code generated at run time in WebAssembly: importing memory, table indices as function pointers, `addFunction`, the limits of synchronous and asynchronous compilation |
