# 서드파티 고지 / Third-Party Notices

`AGENTS.md`의 라이선스 정책에 따라 GPL, LGPL, AGPL 같은 전염성 라이선스 코드는 도입하지 않으며, 의존성을 추가하는 작업은 같은 작업에서 아래에 이름, 버전, 라이선스, 경로를 적습니다.

*Per the license policy in `AGENTS.md`, copyleft-licensed code such as GPL, LGPL or AGPL is not introduced, and a task that adds a dependency records its name, version, license and path below in the same task.*

| 이름 / Name | 버전 / Version | 라이선스 / License | 경로 / Path |
| --- | --- | --- | --- |
| Zydis (+ Zycore) | v4.1.1 | MIT | `third_party/zydis/` |
| Berkeley SoftFloat | Release 3e | BSD 3-Clause | `third_party/softfloat/` |

## Zydis v4.1.1

rex86은 Zydis tag `v4.1.1`(`a2278f1d254e492f6a6b39f6cb5d1f5d515659dc`)과 고정된 Zycore submodule(`0b2432ced0884fd152b471d97ecf0258ff4d859f`)에서 공식 `amalgamate.py`로 생성된 단일 파일 소스를 vendoring합니다. rePIU가 같은 파일을 vendoring하고 다섯 호스트에서 검증했으며, 파일 해시는 rePIU의 고지와 같습니다. 두 프로젝트 모두 MIT 라이선스입니다. 32비트 x86 명령 디코드와 메타데이터에만 사용합니다.

*rex86 vendors the official amalgamated source generated from Zydis tag `v4.1.1` (`a2278f1d254e492f6a6b39f6cb5d1f5d515659dc`) and its pinned Zycore submodule (`0b2432ced0884fd152b471d97ecf0258ff4d859f`). rePIU vendors the identical files, proven on five hosts, and the hashes below match rePIU's notice. Both projects use the MIT License. Used only for 32-bit x86 instruction decoding and metadata.*

- Project: https://github.com/zyantific/zydis
- Zydis license: [`third_party/zydis/LICENSE-ZYDIS`](third_party/zydis/LICENSE-ZYDIS)
- Zycore license: [`third_party/zydis/LICENSE-ZYCORE`](third_party/zydis/LICENSE-ZYCORE)

Generated file SHA-256 values:

- `Zydis.c`: `1851E6D2EC6A681915D3723A96516A387F628ECA6E092A04BEDE78198CDF644A`
- `Zydis.h`: `CE154FC859C134C1DF5BC62A9DFE1C427135812C1A7BC1C54BA180EA27A70E55`

## Berkeley SoftFloat Release 3e

rex86은 [berkeley-softfloat-3](https://github.com/ucb-bar/berkeley-softfloat-3)의 Release 3e 커밋(`f74b1e4`, "Release 3e")에서 `build/template-FAST_INT64/Makefile`이 나열한 302개 C 소스, `source/include/`의 헤더, `8086` specialization(x87 규칙)을 수정 없이 vendoring합니다. 이후 커밋(bf16 추가와 다른 specialization의 오타 수정)은 8086 specialization을 바꾸지 않아 가져오지 않았습니다. `third_party/softfloat/rex86/platform.h`만 이 저장소의 파일입니다(이식 가능한 C, 스레드 로컬 전역). 전이 의존성은 없습니다. x87 80비트 산술에만 사용합니다(#19).

*rex86 vendors, unmodified, the 302 C sources that `build/template-FAST_INT64/Makefile` lists, the headers under `source/include/` and the `8086` specialization (the x87's rules) from the Release 3e commit (`f74b1e4`, "Release 3e") of berkeley-softfloat-3. Later commits (bf16 support and typo fixes in other specializations) leave the 8086 specialization untouched and are not taken. Only `third_party/softfloat/rex86/platform.h` is this repository's (portable C, thread-local globals). There are no transitive dependencies. Used only for the x87's 80-bit arithmetic (#19).*

- Project: https://github.com/ucb-bar/berkeley-softfloat-3 (http://www.jhauser.us/arithmetic/SoftFloat.html)
- License: [`third_party/softfloat/COPYING.txt`](third_party/softfloat/COPYING.txt)

SHA-256 values:

- `COPYING.txt`: `145EA96B4A4A04A1A7738D2A2BF9E830F861971E69606187B018D9E8FC0B95C7`
- `source/` (sorted `sha256sum` listing of every `.c` and `.h`, hashed again): `F08D68ADE22D292157ADCC35B8B10B7AFC20BBAAB01052DABCE51BAB02358479`
