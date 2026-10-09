# #34 작업 지시 : 인터프리터 빠른 경로 / #34 work order : the interpreter fast path

이슈: [#34](https://github.com/reexec/rex86/issues/34) | 설계: [20261009-i034](../design/20261009-i034-interpreter-decode-cache.md) | 로그: [20261009-i034](../work-logs/20261009-i034-interpreter-decode-cache.md)

## 작업 항목 / Tasks

1. 기준선: 지금 코드로 벤치마크(x86-64 GCC Release, Windows x86 MSVC Release)를 다시 잰다. 같은 시점, 같은 기계에서 전후를 비교하기 위해서다.
2. `PageAttributeTable`의 페이지별 세대(`kTranslated`가 지워질 때 + 1)와 `Generation`. `src/interp/decode_cache.{h,cpp}`: 직접 사상 4,096칸, 키, 적중 검사, 삽입, 초기화하지 않는 지연 할당(설계 결정 1, 2).
3. `interp::Step`: 캐시 인자, 적중/놓침 경로, 묶음 인출(결정 3).
4. `Cpu`: 캐시 소유, 게이트가 없을 때 조회 생략(결정 4).
5. 단위 테스트: SMC 넷과 두 Cpu가 메모리를 공유하는 경우(결정 2, 5), 세대 규칙, 캐시 적중 뒤에도 폴트와 limit이 지금과 같음.
6. 견고성 하네스의 I4를 고친다.
7. 검증: 단위 테스트, 세 호스트 대조 fuzz, trace 묶음, 견고성 하네스(ASan/UBSan), 로컬 빌드 전부. 벤치마크 전후.
8. 문서: 분석 `interpreter-performance.md`(전후와 남은 병목), ARCHITECTURE, README 현재 수치, 작업 로그.

*1 re-measure the baseline (x86-64 GCC Release, Windows x86 MSVC Release) at the same time and on the same machine as the after; 2 per-page generations in `PageAttributeTable` (+1 when `kTranslated` clears) with `Generation`, and `decode_cache` (4,096 direct-mapped slots, keys, hit checks, insertion, uninitialized lazy allocation); 3 `interp::Step` with the cache argument, hit and miss paths and bulk fetching; 4 `Cpu` owning the cache and skipping the gate lookup with no gates; 5 unit tests for the four SMC cases, two `Cpu`s sharing memory, the generation rule, and faults and limits unchanged after a hit; 6 the robustness harness's I4; 7 verification with every instrument and the benchmark before and after; 8 the analysis, ARCHITECTURE, README's current figures and the work log.*

## 완료 조건 / Completion criteria

* 벤치마크의 MIPS가 의미 있게 오르고(목표: mixed 2배 이상), 수치를 분석에 적는다.
* 모든 대조 fuzz, trace 묶음, 견고성 하네스, 단위 테스트가 통과한다(결과 불변).
* 다섯 호스트 CI와 libFuzzer 작업이 녹색이다.

*The benchmark's MIPS rises meaningfully (target: mixed at least doubled) with the figures in the analysis; every comparison fuzz, trace corpus, the robustness harness and the unit tests pass (results unchanged); CI is green on the five hosts and the libFuzzer job.*
