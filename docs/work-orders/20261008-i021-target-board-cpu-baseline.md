# #21 작업 지시서 : 최소 지원 사양을 대상 기판의 CPU로 재설정 / #21 work order : resetting the minimum specification to the target boards' CPUs

이슈: [#21](https://github.com/reexec/rex86/issues/21) | 설계: [20261008-i021](../design/20261008-i021-target-board-cpu-baseline.md) | 로그: [20261008-i021](../work-logs/20261008-i021-target-board-cpu-baseline.md)

## 절차 / Steps

1. 네 기판의 CPU를 조사해 `docs/kb/target-board-cpus.md`에 출처와 확실성을 붙여 정리하고 kb 색인을 갱신한다.
   *Research the four boards' CPUs into `docs/kb/target-board-cpus.md` with sources and confidence, and update the kb index.*
2. 설계를 쓴다: 지금 목표와의 차이, 최소 CPU 모델, 기판별 기능 플래그, EFLAGS AC/ID, 기판별 성능 기준, 단계 갱신.
   *Write the design: the differences from today's targets, the minimum CPU model, per-board feature flags, EFLAGS AC/ID, per-board performance references, the phase update.*
3. README 목표 1, 3과 경고 문단, 달성도 표를 고친다. 헌장에 지원 CPU 사양 절을 더한다. ARCHITECTURE에 `Features`의 역할을 적는다.
   *Update README goals 1 and 3, the status note and the attainment table; add the supported CPU section to the charter; record `Features`'s role in ARCHITECTURE.*
4. 링크를 검사하고 작업 로그를 쓴다.
   *Check the links and write the work log.*

## 완료 조건 / Done when

README, 헌장, ARCHITECTURE가 "P6 정수 + P6 x87 + MMX + SSE, SSE2 이후 제외, 3DNow!는 census 조건부, 기판별 실시간 기준"을 같은 말로 적고, 모든 새 링크가 실제 파일을 가리키며, CI가 녹색이다. 코드는 바뀌지 않는다.

*README, the charter and ARCHITECTURE state the same scope (P6 integer, P6 x87, MMX, SSE; nothing past SSE; 3DNow! census-conditional; real time per board CPU), every new link resolves, and CI is green. No code changes.*
