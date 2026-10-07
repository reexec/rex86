# 서드파티 고지 / Third-Party Notices

`AGENTS.md`의 라이선스 정책에 따라 GPL, LGPL, AGPL 같은 전염성 라이선스 코드는 도입하지 않으며, 의존성을 추가하는 작업은 같은 작업에서 아래에 이름, 버전, 라이선스, 경로를 적습니다.

설계 문서가 채택 후보로 적어 둔 것 중 남은 것: Berkeley SoftFloat 3(BSD 3-Clause, 80비트 x87). 도입 작업에서 고정 revision과 전이 의존성의 라이선스를 다시 확인합니다.

*Per the license policy in `AGENTS.md`, copyleft-licensed code such as GPL, LGPL or AGPL is not introduced, and a task that adds a dependency records its name, version, license and path below in the same task. The remaining adoption candidate from the design is Berkeley SoftFloat 3 (BSD 3-Clause, 80-bit x87); its adopting task rechecks the pinned revision and transitive licenses.*

| 이름 / Name | 버전 / Version | 라이선스 / License | 경로 / Path |
| --- | --- | --- | --- |
| Zydis (+ Zycore) | v4.1.1 | MIT | `third_party/zydis/` |

## Zydis v4.1.1

rex86은 Zydis tag `v4.1.1`(`a2278f1d254e492f6a6b39f6cb5d1f5d515659dc`)과 고정된 Zycore submodule(`0b2432ced0884fd152b471d97ecf0258ff4d859f`)에서 공식 `amalgamate.py`로 생성된 단일 파일 소스를 vendoring합니다. rePIU가 같은 파일을 vendoring하고 다섯 호스트에서 검증했으며, 파일 해시는 rePIU의 고지와 같습니다. 두 프로젝트 모두 MIT 라이선스입니다. 32비트 x86 명령 디코드와 메타데이터에만 사용합니다.

*rex86 vendors the official amalgamated source generated from Zydis tag `v4.1.1` (`a2278f1d254e492f6a6b39f6cb5d1f5d515659dc`) and its pinned Zycore submodule (`0b2432ced0884fd152b471d97ecf0258ff4d859f`). rePIU vendors the identical files, proven on five hosts, and the hashes below match rePIU's notice. Both projects use the MIT License. Used only for 32-bit x86 instruction decoding and metadata.*

- Project: https://github.com/zyantific/zydis
- Zydis license: [`third_party/zydis/LICENSE-ZYDIS`](third_party/zydis/LICENSE-ZYDIS)
- Zycore license: [`third_party/zydis/LICENSE-ZYCORE`](third_party/zydis/LICENSE-ZYCORE)

Generated file SHA-256 values:

- `Zydis.c`: `1851E6D2EC6A681915D3723A96516A387F628ECA6E092A04BEDE78198CDF644A`
- `Zydis.h`: `CE154FC859C134C1DF5BC62A9DFE1C427135812C1A7BC1C54BA180EA27A70E55`
