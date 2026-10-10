# #42 작업 로그 : 3단계 설계 / #42 work log : the phase 3 design

이슈: [#42](https://github.com/reexec/rex86/issues/42) | 설계: [20261010-i042](../design/20261010-i042-phase3-translation.md)

## 2026-10-10

- **맥락 확인**: 3단계에 관한 기존 결정을 모았다. #1 설계 결정 5(공용 IR 위의 백엔드 둘, wasm은 호스트 JS가 비동기로 인스턴스화, 실행 시점의 엔진 선택), #31 설계 결정 6(견고성 하네스의 백엔드 차등), #34 결정 2(`kTranslated`와 페이지 세대), 공개 계약의 `CodeCacheServices`(실행 메모리, AArch64용).
- **실험**(emsdk 3.1.74, Node 24.19, AMD Ryzen 5 5600X, scratchpad의 버리는 코드라 저장소에 넣지 않았다): C++이 만든 68바이트 wasm 모듈을 JS 라이브러리가 같은 `wasmMemory`로 동기 인스턴스화하고 `addFunction`(`-sALLOW_TABLE_GROWTH`)으로 테이블에 넣었다. C++은 인덱스를 함수 포인터로 바꿔 불렀다. 결과가 맞았고(41 → 42, 1,000만 번 뒤 10,000,041), 호출 하나가 3.0 ns, 설치 하나가 216 µs였다. 설계 결정 8의 근거다.
- **조사**: Chrome의 메인 스레드 동기 컴파일 한도가 4 KB에서 8 MB로 올라간 Chromium 커밋을 확인했다. kb `wasm-runtime-code.md`에 출처와 함께 적었다.
- **설계**: 결정 1~11과 소비자 영향, 미확정을 썼다. 코드 변경은 없다(하위 이슈에서 한다).

*Context: the existing phase 3 decisions were gathered (#1 design decision 5, #31 decision 6, #34 decision 2, and the public contract's `CodeCacheServices` for executable memory, meant for AArch64). Spike (emsdk 3.1.74, Node 24.19, AMD Ryzen 5 5600X; throwaway scratchpad code, not committed): a 68-byte wasm module built in C++ was instantiated synchronously by a JS library against the same `wasmMemory` and added to the table with `addFunction` (`-sALLOW_TABLE_GROWTH`); C++ called the index as a function pointer with the right result (41 to 42, 10,000,041 after 10M calls), 3.0 ns per call and 216 µs per installation, the basis of design decision 8. Research: the Chromium commit raising the main-thread synchronous compile cap from 4 KB to 8 MB, recorded with sources in the kb `wasm-runtime-code.md`. Design: decisions 1 to 11, consumer impact and unresolved items; no code change (that comes with the sub-issues).*
