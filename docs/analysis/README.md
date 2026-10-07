# 분석 색인 / Analysis Index

이 디렉터리는 이 프로젝트가 **직접 확인한** 사실을 주제별로 누적한다. 호스트 CPU와 대조해 측정한 명령 의미, 호스트별 차이, 소비자 프로젝트의 게스트가 실제로 쓰는 명령 집합(census)이 여기 들어간다. 일반 기술 배경은 [`docs/kb/`](../kb/README.md)에 둔다.

*This directory accumulates facts **verified directly** by this project, organized by topic: instruction semantics measured against a host CPU, differences between hosts, and the instruction set a consumer's guest actually uses (the census). General background knowledge lives in [`docs/kb/`](../kb/README.md).*

## 표기 규칙 / Notation

모든 서술은 **확인됨 / 추정 / 미확정** 중 하나로 표기한다. 확인됨에는 검증 방법을, 추정에는 근거를, 미확정에는 확인 방법을 함께 적는다.

*Every statement is marked **confirmed**, **inferred** or **unresolved**, alongside the verification method, the evidence, or the way to find out.*

## 문서 / Documents

| 문서 | 내용 | 현재 상태 |
| --- | --- | --- |
| [x87-host-comparison.md](x87-host-comparison.md) | x87 호스트 CPU 대조(AMD Zen 3) 실측: SDM이 정하지 않은 동작, FPREM 부분 감소 N, 예외 우선순위, SDM 이탈 1종 / the x87 host-CPU comparison (AMD Zen 3): behavior the SDM leaves open, FPREM's partial reduction, exception priority, one SDM deviation | #19에서 확인(4,000만 건 불일치 0), Intel 미확정 / confirmed in #19 (40M cases, zero mismatches), Intel unresolved |
| [singlesteptests-386ex-deviations.md](singlesteptests-386ex-deviations.md) | SingleStepTests 386EX 실측: 테스트 구조, SDM과 다른 하드웨어 동작(SIB scale-on-base, POPAD ESP), 인코딩 해석 주의 / facts measured from the 386EX suite: structure, SDM deviations, encoding notes | #11에서 확인, #17에서 unreal 결론 정정과 예외 전달 추가 / confirmed in #11, corrected and extended in #17 |
