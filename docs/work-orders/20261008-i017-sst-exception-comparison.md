# #17 작업 지시 : SST 예외 의미 비교 / #17 work order : SST exception comparison

이슈: [#17](https://github.com/reexec/rex86/issues/17) | 설계: [20261008-i017](../design/20261008-i017-sst-exception-comparison.md) | 로그: [20261008-i017](../work-logs/20261008-i017-sst-exception-comparison.md)

## 작업 항목 / Tasks

1. `include/rex86/environment.h`: `FaultKind::kStackFault`(설계 결정 2). `src/interp/access.cpp`: `Linearize`가 SS면 `kStackFault`.
2. `src/interp/interpreter.cpp`: CS limit에서 잘린 인출의 `kGeneralProtection`(결정 5).
3. `rex86_sst`: 디스크립터 limit 0xFFFF(결정 1), 폴트·소프트웨어 인터럽트의 IVT 전달과 핸들러 HLT까지 실행, 벡터와 push된 FLAGS 비교(결정 3), 새 집계(전달 수, 이중 폴트).
4. 단위 테스트(설계 검증 절), SST 전체 실행과 남는 건의 분류.
5. 문서: README(경고 상자, 달성도), ARCHITECTURE(필요 시), `docs/analysis/singlesteptests-386ex-deviations.md`(unreal 결론 정정과 예외 실측), `docs/guides/singlesteptests.md`(실행 비교 절차), `docs/kb/singlesteptests-moo.md`(EXCP의 의미).

*1 add `FaultKind::kStackFault` and raise it from `Linearize` for SS; 2 raise `kGeneralProtection` for a fetch truncated at the CS limit; 3 give the harness 0xFFFF limits, IVT delivery for faults and software interrupts with execution to the handler's HLT, the vector and pushed-FLAGS comparisons and the new tallies; 4 unit tests, the full SST run and the classification of what remains; 5 the documents — README (warning box, attainment), ARCHITECTURE if needed, the analysis topic (correcting 'unreal', recording the exception measurements), the SST guide and the MOO kb entry.*

## 완료 조건 / Completion criteria

* 전체 SST mismatches 0, `skipped_exception` 대폭 감소(남는 것은 근거와 함께 로그에), 기존 실행 건수 유지 또는 증가.
* 단위 테스트 전체 통과, 로컬 빌드 깨끗. 공개 헤더 변경은 `FaultKind::kStackFault` 하나.

*Zero SST mismatches with `skipped_exception` sharply down (whatever remains logged with reasons) and the executed count kept or raised; all unit tests pass on a clean local build; the only public-header change is `FaultKind::kStackFault`.*
