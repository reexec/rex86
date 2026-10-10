# #44 작업 지시 : 번역 실행기 / #44 work order : the translation runtime

이슈: [#44](https://github.com/reexec/rex86/issues/44) | 설계: [20261010-i044](../design/20261010-i044-translation-runtime.md) | 로그: [20261010-i044](../work-logs/20261010-i044-translation-runtime.md)

## 작업 항목 / Tasks

1. 인터프리터: `StepResult::branched`, `BlockLimits::stop_after_branch`, `RunBlock`의 두 판(설계 결정 2).
2. `src/translate/runtime.{h,cpp}`: `Translator`, 캐시, 유효성, 계층화, 번역 불가 기억, 크기 한도, 평가기 백엔드(결정 3).
3. `Cpu`: `TranslationOptions`, `SetTranslation`, 디스패치(결정 1), `RegisterGate`의 비움, `ActiveEngine`, `EngineMemoryBytes`, CMake `REX86_FORCE_TRANSLATION`(결정 4).
4. 견고성 하네스: 번역 실행과 불변식 I8(결정 5).
5. 단위 테스트 `tests/unit/translate_runtime_test.cpp`(결정 5).
6. CI: 번역 강제 작업 하나.
7. 검증: 로컬 구성 전부와 번역 강제 구성(x86-64, i386, wasm32), 긴 견고성 차등, cachegrind로 인터프리터 비용 확인.
8. 문서: #42 설계 결정 9의 SST, ARCHITECTURE, 견고성 가이드와 분석(I8), 작업 로그.

*1 interpreter: `StepResult::branched`, `BlockLimits::stop_after_branch` and two versions of `RunBlock` (design decision 2); 2 `src/translate/runtime.{h,cpp}`: the `Translator`, cache, validity, tiering, remembered untranslatable EIPs, size cap and evaluator backend (decision 3); 3 `Cpu`: `TranslationOptions`, `SetTranslation`, dispatch (decision 1), `RegisterGate` dropping translations, `ActiveEngine`, `EngineMemoryBytes`, and the CMake option `REX86_FORCE_TRANSLATION` (decision 4); 4 the robustness harness: a translated run and invariant I8 (decision 5); 5 unit tests in `tests/unit/translate_runtime_test.cpp` (decision 5); 6 CI: one forced-translation job; 7 verification: every local configuration plus forced translation (x86-64, i386, wasm32), a long robustness differential, and the interpreter's cost by cachegrind; 8 documents: SST in #42's decision 9, ARCHITECTURE, the robustness guide and analysis (I8), the work log.*

## 완료 조건 / Completion criteria

* 번역 강제 구성의 ctest가 x86-64, i386, wasm32와 CI 강제 작업에서 통과한다.
* 견고성 차등(I8)이 긴 실행에서 위반 0이다.
* 번역이 꺼진 인터프리터의 단계당 호스트 명령이 바뀌지 않는다(잡음 범위).
* 다섯 호스트 CI와 libFuzzer 작업이 녹색이다.

*The forced-translation ctest passes on x86-64, i386 and wasm32 and in the CI forced job; the robustness differential (I8) has zero violations over a long run; the interpreter's host instructions per step with translation off are unchanged (within the noise); CI is green on the five hosts and the libFuzzer job.*
