# #65 작업 로그 : 번역 경로 성능 / #65 work log : translation path performance

이슈: [#65](https://github.com/reexec/rex86/issues/65) | 설계: [20261014-i065](../design/20261014-i065-translation-dispatch.md) | 지시서: [20261014-i065](../work-orders/20261014-i065-translation-dispatch.md)

## 2026-10-14

호스트 F: Intel Core i5-7200U(2코어 4스레드), Windows 10의 WSL2 Ubuntu 24.04. GCC 13.3, emsdk 6.0.8(Node 24.19.0). 브랜치는 `work/i065-translation-dispatch`(main `f12e13b`, v0.0.31에서)다. 이슈를 만들 때 번호가 #64가 아니라 #65로 나왔다. #64는 그 사이 다른 곳에서 머지된 PR(Android Chrome 측정, v0.0.31)이었다. 브랜치 이름을 바꾸고 그 main 위에서 시작했다.

*Host F: an Intel Core i5-7200U (2 cores, 4 threads), WSL2 Ubuntu 24.04 under Windows 10, GCC 13.3, emsdk 6.0.8 (Node 24.19.0); branch `work/i065-translation-dispatch` from main `f12e13b` (v0.0.31). The issue came out as #65 rather than #64, #64 being a PR merged elsewhere meanwhile (the Android Chrome measurement, v0.0.31); the branch was renamed and started on that main.*

### 측정(설계 결정 1)

- 블록 통계(Node, wasm, 10프레임): alu 49 MIPS에 블록당 9.0명령, memory 65에 8.0, call 54에 6.3이다. 블록당 약 120~180 ns다.
- `--profiling-funcs` 빌드의 `node --cpu-prof`(200 µs 간격)로 자기 시간을 봤다. 디스패치가 32~42%, 메모리 검사 도우미가 memory 18%와 call 30%, 블록 본체가 23~26%였다(설계의 표).
- 디스패치 안을 나누려고 세 가지를 해 봤다.
  - `-fno-inline`: binaryen이 링크 때 다시 인라인하고 같은 함수를 합쳐, `vector` 같은 엉뚱한 이름이 섞였다.
  - 네이티브 `gprof`(평가기): 평가기 본체가 대부분이라 wasm 경로를 대신하지 못했다. perf와 valgrind는 없다(sudo 필요).
  - **링크를 `-O0`으로**(각 파일은 `-O3` 그대로): 파일 사이 인라인이 일어나지 않아 함수별로 나뉘었다. alu에서 해시 조회 14~16%, `Lookup` 14~15%, `RunUntilStop` 14%, `WasmBackend::Run` 7~8%, 본체 23~24%였다.

*Block statistics (Node, wasm, 10 frames): alu at 49 MIPS with 9.0 instructions a block, memory at 65 with 8.0, call at 54 with 6.3, some 120 to 180 ns a block. A `--profiling-funcs` build under `node --cpu-prof` (200 µs interval) gave self time: dispatch 32 to 42%, the memory-check helper 18% on memory and 30% on call, the block body 23 to 26% (the design's table). Splitting dispatch took three tries: `-fno-inline` (binaryen inlined again at link time and merged identical functions, mixing in names like `vector`); native `gprof` on the evaluator (its body dominates, no stand-in for the wasm path; perf and valgrind are absent, needing sudo); and **linking at `-O0`** with each file still at `-O3`, which keeps functions apart across files: on alu the hash lookup took 14 to 16%, `Lookup` 14 to 15%, `RunUntilStop` 14%, `WasmBackend::Run` 7 to 8% and the body 23 to 24%.*

### 디스패치 빠른 경로(결정 2)

- 첫 판(안쪽 고리와 `Lookup` 앞의 캐시)을 main과 번갈아 쟀는데, 같은 빌드끼리도 2~3배씩 흔들렸다. 그 시간 Windows 쪽 CPU 사용률이 56%였다(VS Code, Edge). WSL 안의 `journalctl`도 65%를 썼다. 그래서 두 가지 잣대로 바꿨다.
  - **본체 몫**: 링크 최적화를 끈 프로파일에서 블록 본체가 차지하는 몫. 본체의 일은 그대로이니 몫이 커진 만큼 나머지가 준 것이다.
  - **일곱 번 중 최고값**: 부하는 시간을 늘리기만 하므로 가장 좋은 값이 그 빌드의 속도에 가장 가깝다.
- 첫 판의 본체 몫은 alu 23.7% → 27.4%였다. 해시 조회는 사라졌지만, 남은 일이 다른 파일의 작은 함수 호출로 흩어져 있었다. `ModesSupported`, `PageAttributeTable::Generation`, 캐시가 맞는 경우의 `Lookup`과 `StillValid`를 헤더 인라인으로 옮겼다. 나머지는 `LookupSlow`다.
- 둘째 판의 본체 몫: alu 22.6% → 29.0%, memory 24.5% → 31.2%, call 23.5% → 26.0%다. 블록당 시간으로 약 22%, 21%, 10% 줄었다.

*The first version (the inner loop and a cache in front of `Lookup`) measured against main swung two to threefold even within one build, with Windows at 56% CPU (VS Code, Edge) and `journalctl` in WSL at 65%. So two yardsticks took over: the block body's share in profiles with link-time optimization off (the body's work unchanged, a larger share meaning the rest shrank), and the best of seven runs (load only adds time, so the best is closest to the build's own speed). The first version moved alu's body share from 23.7% to 27.4%: the hash was gone, but the rest was spread over small calls into other files, so `ModesSupported`, `PageAttributeTable::Generation`, and `Lookup`'s cache path with `StillValid` became inline in headers, the rest being `LookupSlow`. The second version's body share: alu 22.6% to 29.0%, memory 24.5% to 31.2%, call 23.5% to 26.0%, some 22, 21 and 10% less time a block.*

### 인라인 메모리 검사(결정 3)

- `codegen`: `InlineCheck`, `PageFlags`, 임시 지역 셋, `CheckContext`. `backend`가 문맥을 두고 `Run` 전에 채운다. 공개 헤더에 `PageAttributeTable::data()`를 더했다.
- `WasmCheckTests`: 아홉 경우 모두 번역 실행과 인터프리터의 이벤트, 상태, 메모리가 같다(checks 2,344 → 2,362).
- 뒤의 프로파일에서 `AccessWouldSucceed`와 `CheckHelper`가 사라졌다.

*`codegen` gains `InlineCheck`, `PageFlags`, three temporary locals and `CheckContext`; `backend` keeps the context and fills it before `Run`; the public header gains `PageAttributeTable::data()`. `WasmCheckTests`: in all nine cases the translated run's events, state and memory equal the interpreter's (checks 2,344 to 2,362). In the profiles after it, `AccessWouldSucceed` and `CheckHelper` are gone.*

### 결과와 검증

- main 대비(wasm, 일곱 번 번갈아 최고값, 부하 평균 4~5): alu 44.1 → 56.2(+28%), memory 28.7 → 39.3(+37%), call 25.6 → 43.4(+69%), blocks 1.91 → 1.87(같음), mixed 14.6 → 15.5(+6%). 같은 표가 설계와 [분석](../analysis/interpreter-performance.md) 2.6절에 있다.
- 시험(로컬):
  - 네이티브 Release ctest 9/9, 번역 강제(평가기) ctest 9/9(결정 2 뒤).
  - wasm32 ctest 8/8, 번역 강제 `WASM` ctest 8/8(결정 3 뒤).
  - wasm 단위 테스트 checks 2,362, 실패 0.

*Against main (wasm, best of seven interleaved, load average 4 to 5): alu 44.1 to 56.2 (+28%), memory 28.7 to 39.3 (+37%), call 25.6 to 43.4 (+69%), blocks 1.91 to 1.87 (unchanged), mixed 14.6 to 15.5 (+6%); the same table is in the design and section 2.6 of the analysis. Local tests: native Release ctest 9/9 and forced translation (evaluator) ctest 9/9 after decision 2; wasm32 ctest 8/8 and forced `WASM` ctest 8/8 after decision 3; wasm unit tests 2,362 checks, 0 failures.*

### 다음 / Next

1. CI(Windows, Clang, 새니타이저, 번역 강제, 브라우저 셋)를 확인한다.
2. 후속 후보: 블록 연결(남은 디스패치 약 30%), REP 문자열 번역, x87(도우미 호출), 인터프리터의 세밀한 무효화(#60).

*1 Check CI (Windows, Clang, sanitizers, forced translation, three browsers). 2 Follow-up candidates: block linking (dispatch, still some 30%), translating REP strings, the x87 (helper calls), and finer invalidation in the interpreter (#60).*
