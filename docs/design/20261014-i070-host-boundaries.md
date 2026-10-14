# #70 설계 : 호스트 경계 명령 / #70 design : host boundary instructions

이슈: [#70](https://github.com/reexec/rex86/issues/70) | 지시서: [20261014-i070](../work-orders/20261014-i070-host-boundaries.md) | 로그: [20261014-i070](../work-logs/20261014-i070-host-boundaries.md)

## 배경 / Background

rePIU의 `emulated` 실행 모델([rePIU #61](https://github.com/reexec/rePIU/issues/61))은 운영 경로인 AOT `dynamic` 백엔드와 같은 규칙으로 실행해야 한다. 그 경로는 세그먼트를 호스트 평탄 그대로 두고, AOT가 분류한 "HLE 경계 명령"(세그먼트 override 접두어, 세그먼트 레지스터 피연산자, LAR/LSL 등)만 실행 전에 HLE로 넘긴다. 이 명령들은 x86 의미로는 폴트가 나지 않으므로 #69의 `user_mode`로는 멈출 수 없다. rePIU에서 가상 셀렉터를 디스크립터로 넣는 방법을 먼저 시도했으나, rePIU HLE의 세그먼트 의미가 디스크립터 모델이 아니어서 맞지 않았다(rePIU 설계 #61 결정 9).

*rePIU's `emulated` model must run by the production path's rules: segments stay the host's flat ones and only the instructions the AOT classifies as HLE boundaries (segment override prefixes, segment register operands, LAR/LSL and the like) go to the HLE before they run. They do not fault under x86 semantics, so #69's `user_mode` cannot stop them. Putting the virtual selectors into descriptors was tried first in rePIU and did not fit, because rePIU's HLE gives segments their meaning by rules other than descriptors (rePIU design #61, decision 9).*

## 결정 / Decisions

1. **계약:** `Environment::IsHostBoundary(bytes, length)`(기본 false), `Features::host_boundaries`(기본 꺼짐), `StopReason::kHostBoundary`(EIP는 그 명령, 실행 안 됨, `gate_address`에 선형 주소), `Cpu::SkipHostBoundaryOnce()`(현재 CS:EIP의 경계 명령을 다음 실행에서 한 번 그대로 실행). 호스트는 경계에서 자기 처리를 하거나, 처리하지 않기로 하면 건너뛰기를 요청한다. 그러면 명령은 보통의 x86 의미로 실행되고 폴트가 나면 폴트로 돌아온다.
2. **비용:** 질의는 디코드마다 한 번이다. 답은 `DecodedInstruction::host_boundary`에 저장되어 디코드 캐시 항목과 수명이 같다. 꺼져 있으면 비교 하나도 하지 않는다.
3. **위치:** `interp::ExecuteDecoded`에서 `FeatureEnabled` 뒤, `user_mode` 검사 앞이다. 그래서 호스트가 INT를 경계로 정하면 #GP 대신 경계로 먼저 멈춘다. 건너뛰기로 실행하면 그때 `user_mode` 의미가 적용된다.
4. **번역:** IR 프런트엔드는 경계 명령을 블록에 넣지 않는다(`FrontendOptions::boundaries`, 번역 제한을 통해 `Cpu`가 넘김). 블록이 그 앞에서 인터프리터로 나가므로 경계는 언제나 인터프리터에서 관측된다.
5. **REP 재개:** 반복 사이에서 멈춘 REP 문자열의 재개는 시작할 때 이미 검사를 지났으므로 다시 묻지 않는다.

*(1) The contract: `Environment::IsHostBoundary(bytes, length)` (default false), `Features::host_boundaries` (default off), `StopReason::kHostBoundary` (EIP at the instruction, not run, its linear address in `gate_address`) and `Cpu::SkipHostBoundaryOnce()` (the next run executes the boundary at the current CS:EIP once, as it is). The host handles the boundary itself, or asks to skip it; the instruction then runs with ordinary x86 semantics and comes back as a fault if it faults. (2) Cost: one question per decode, the answer kept in `DecodedInstruction::host_boundary` with the decode cache entry's lifetime; when off, not even a compare. (3) Placement: `interp::ExecuteDecoded`, after `FeatureEnabled` and before the `user_mode` check, so a host that makes INT a boundary sees the boundary before the #GP, and `user_mode` applies when it is skipped. (4) Translation: the IR frontend never takes a boundary into a block (`FrontendOptions::boundaries`, passed by `Cpu` through the translator limits), so boundaries are always observed by the interpreter. (5) A REP string resumed between iterations passed its check when it started and is not asked again.*

## 소비자 영향 / Effect on consumers

rePIU는 AOT의 `IsHleBoundary` 분류로 이 훅을 구현한다. re2DJ는 바뀌지 않는다(기본 꺼짐).

*rePIU implements the hook with the AOT's `IsHleBoundary` classification; re2DJ is unaffected (off by default).*

## 검증 / Verification

`host_boundary_test.cpp`: 꺼짐 기본값, 실행 전 멈춤과 상태, 한 번 건너뛰기와 EIP가 바뀌면 무효, 캐시된 디코드는 한 번만 질의(5만 바퀴 고리에서 질의 5만 미만), 번역(평가기, 문턱 0)에서도 경계 앞 멈춤.

*`host_boundary_test.cpp`: the default off, the stop before execution with the state intact, skipping once and its lapse when EIP moves, one question per cached decode (fewer than 50,000 over a 50,000-pass loop), and the stop before a boundary under translation (the evaluator at threshold 0).*
