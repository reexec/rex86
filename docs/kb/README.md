# 기술 지식 기반 색인 / Knowledge Base Index

이 디렉터리는 **일반적으로 통용되는** 기술 배경 지식을 주제별로 둔다. IA-32 명령 인코딩과 의미, x87 80비트 형식, WebAssembly와 AArch64의 특성, 호스트 OS의 JIT 제약이 그 예다. 이 프로젝트가 직접 확인한 사실은 [`docs/analysis/`](../analysis/README.md)에 둔다.

*This directory holds **generally applicable** technical background, organized by topic: IA-32 encodings and semantics, the x87 80-bit format, properties of WebAssembly and AArch64, host-OS JIT constraints. Facts this project verified itself live in [`docs/analysis/`](../analysis/README.md).*

외부 자료에서 얻은 내용에는 Intel SDM, Arm Architecture Reference Manual, WebAssembly 사양, 공식 프로젝트 문서 같은 권위 있는 출처 링크를 가까운 위치에 남긴다.

*Place authoritative links (the Intel SDM, the Arm Architecture Reference Manual, the WebAssembly specification, official project documentation) near anything derived from an external source.*

## 문서 / Documents

| 문서 | 내용 |
| --- | --- |
| [x87.md](x87.md) | x87 FPU: 80비트 형식과 분류, 레지스터 스택과 태그, CW/SW, 예외와 우선순위, 환경 이미지 / the 80-bit format and its classes, the register stack and tags, CW/SW, exceptions and their priority, environment images |
| [singlesteptests-moo.md](singlesteptests-moo.md) | SingleStepTests 하드웨어 생성 테스트와 MOO v1.1 바이너리 형식 / the hardware-generated SingleStepTests suites and the MOO v1.1 binary format |
