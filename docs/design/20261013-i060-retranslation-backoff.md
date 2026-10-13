# #60 설계 : 자기 수정 코드의 재번역 억제와 모듈 수 대 처리량 / #60 design : damping re-translation under self-modifying code, and module count against throughput

이슈: [#60](https://github.com/reexec/rex86/issues/60) | 지시서: [20261013-i060](../work-orders/20261013-i060-retranslation-backoff.md) | 로그: [20261013-i060](../work-logs/20261013-i060-retranslation-backoff.md) | 근거: [#49 설계](20261012-i049-wasm-module-batching.md) 결정 3

## 문제 / Problem

코드가 든 페이지에 쓰면 그 페이지의 세대가 오르고, 그 페이지의 번역과 디코드가 모두 버려진다(#34 결정 2, 페이지 단위). 자기 수정 코드만 그런 것이 아니다. 옛 게임은 변수를 코드 옆에 두는 일이 흔하다. 그런 페이지의 블록은 문턱(기본 32번 진입)에 닿을 때마다 다시 번역되고, 곧 다시 버려진다. 번역 비용(블록 형성, 최적화, wasm 모듈 생성)만 쌓인다. #49는 축출에 같은 문제가 있어 억제를 두었고, 코드 변경 쪽은 후속으로 남겼다.

*A write to a page holding code raises the page's generation and drops every translation and decode on it (#34 decision 2, per page). Self-modifying code is not the only cause: old games often keep variables beside their code. A block on such a page is translated again each time it reaches the threshold (32 entries by default) and dropped again soon after, piling up translation cost (forming, optimizing, creating wasm modules). #49 met the same problem with eviction and added a backoff, leaving code changes as a follow-up.*

## 결정 1: 측정용 워크로드 `smc` / Decision 1: the `smc` measurement workload

`rex86_bench`에 일곱째 커널 `smc`를 더한다. 이 커널만 한 페이지를 차지하고, 그 페이지는 게스트가 쓸 수 있고 실행할 수 있다(`kPageReadWriteExecute`).

- 4,096번 도는 고리다. 반복마다 카운터를 그 페이지 안의 dword 변수에 쓴다(코드 옆의 데이터). 16번째 반복마다 고리 첫 ADD의 즉치값을 고친다(진짜 자기 수정).
- 진입할 때 즉치값을 1로 되돌린다. 그래서 바퀴마다 같은 결과가 나오고, C++ 모델 `SmcModel`과 맞춰 본다.
- 저장 주소는 같은 주소에서 미리 한 번 조립해(rehearsal) 얻는다. 절대 주소 피연산자는 늘 disp32라 두 번의 배치가 같다.
- `mixed`에는 넣지 않는다. 버전 사이 비교를 위해서다(#49와 같은 이유).

*`rex86_bench` gains a seventh kernel, `smc`, alone on a page the guest may write as well as execute (`kPageReadWriteExecute`): a loop of 4,096 iterations storing the counter into a dword on that page each iteration (data beside code) and rewriting the immediate of the loop's first ADD every 16th (true self-modification). Its entry resets the immediate, so every lap gives the same result, checked against the C++ model `SmcModel`. The stores' addresses come from a rehearsal assembled at the same address, absolute operands always being disp32. It stays out of `mixed`, so that figures compare across versions (as in #49).*

## 결정 2: 진입은 코드가 그대로인 동안만 센다 / Decision 2: entries count only while the code stays

블록 항목은 세기 시작할 때의 머리 페이지 세대를 기억한다(`count_generation`). 진입할 때 세대가 다르면 진입 수를 0부터 다시 센다. 그래서 블록은 "머리 페이지가 바뀌지 않은 채로 문턱만큼 진입했을 때" 번역된다.

- 쓰기가 문턱보다 잦은 페이지의 블록은 아예 번역되지 않는다. 번역이 살아남아 쓰일 수 없으니 맞는 결과다.
- 비용은 세는 동안 진입마다 `Generation()` 읽기 하나다. 번역되어 도는 블록은 세지 않으므로 이 비용이 없다.
- 머리 페이지만 본다. 블록이 둘째 페이지에 걸치면 그 페이지의 변경은 결정 3이 맡는다.

*A block's entry remembers its head page's generation when the count began (`count_generation`); an entry finding another generation starts the count over from 0, so a block is translated after the threshold's entries with its head page unchanged. A block on a page written more often than the threshold is never translated, which is right, since its translation could not live to be used. The cost is one `Generation()` read per entry while counting; translated blocks, which no longer count, pay nothing. Only the head page is watched; changes to a second page a block spans are decision 3's.*

## 결정 3: 코드 변경도 억제를 올린다 / Decision 3: code changes back off too

#49의 축출 억제 횟수(`evictions`)를 `backoff`로 바꾸고, 코드가 바뀌어 번역이 버려질 때도 올린다. 대기열에서 무효가 된 경우(`Submit`)와 설치된 뒤 무효가 된 경우(`Lookup`) 모두다. 다시 번역되기까지 진입이 문턱의 2배, 4배, … 최대 64배 필요하다(`kMaxBackoffShift` 6). 문턱이 0이어도 진입 1을 기준으로 한다.

- 결정 2만으로는 쓰기가 드문 페이지를 막지 못한다. 예를 들어 문턱보다 오래 살지만 금방 다시 버려지는 번역이 그렇다. 버려질 때마다 기다림이 두 배가 되므로, 번역이 쓰이는 시간보다 기다림이 길어지면 번역이 멈춘다.
- 억제는 `Flush`에서만 지운다. 번역 불가 판정이나 설치 실패로 항목을 되돌릴 때도 남긴다.
- 상한 64배는 #49와 같다. 한 번 쓰인 코드가 그 뒤로 바뀌지 않으면(로더의 패치 등) 늦어지는 것은 최대 64 × 32 = 2,048번 진입뿐이다.
- 공개 계약 `TranslationStats`에 `blocks_invalidated`를 더한다. 코드가 바뀌어 버려진 번역(대기 중 포함)의 수다. 소비자가 게스트의 자기 수정 정도를 볼 수 있다. `rex86_bench`는 이 값을 `modules_submitted=` 줄에 적는다.

*#49's eviction count (`evictions`) becomes `backoff` and also rises when a translation is dropped because its code changed, whether found stale in the queue (`Submit`) or after installation (`Lookup`): the block then needs 2, 4, … up to 64 times the threshold in entries (`kMaxBackoffShift` 6), counting from one entry at a threshold of 0. Decision 2 alone does not stop a page written rarely, a translation outliving the threshold but dropped soon after; doubling the wait at each drop stops translating once the wait outlasts a translation's use. Only `Flush` clears the backoff; an entry reset by an untranslatable verdict or a failed installation keeps it. The 64-fold cap is #49's: code that stops changing once loaded (a loader's patches) is delayed at most 64 × 32 = 2,048 entries. The public contract's `TranslationStats` gains `blocks_invalidated`, the translations (waiting ones included) dropped because their code changed, so a consumer can see how much its guest rewrites code; `rex86_bench` prints it on its `modules_submitted=` line.*

## 결정 4: 모듈 수와 처리량 / Decision 4: module count against throughput

#49는 묶음 크기 16을 기본값으로 골랐다. 그때 블록이 많은 워크로드에서는 64가 조금 빨랐지만, 그 차이가 설치 비용인지 설치 뒤의 호출 비용인지는 나누지 않았다. 여기서 나눴다(수치는 [분석](../analysis/browser-wasm.md) 3.2절).

- **확인됨**: `blocks`에서 묶음 64는 Chromium, Firefox, Node 모두 16보다 빠르다. 모든 블록이 설치된 뒤의 프레임도 약 14% 빠르다. 살아 있는 모듈의 수 자체가 호출 비용에 든다.
- **확인됨**: 대가는 첫 프레임이다. 모듈 하나의 설치가 길어져 첫 프레임이 늦어진다.
- **확인됨**: 작업 집합이 모듈 예산을 넘으면(묶음 1과 4) 축출 억제가 상한에 닿아도 인터프리터보다 느리다. 기본값으로는 블록 6만 5천 개까지 그런 일이 없다.
- **결정**: 기본값(묶음 16, 예산 4,096)은 바꾸지 않는다. 묶음을 키우는 것은 첫 프레임과의 맞바꿈이다. 블록 2만 4천여 개의 합성 워크로드만으로 정하지 않고, 소비자 게스트(2A/2B 단계)의 측정과 함께 정한다. 소비자는 `blocks_per_module`로 지금도 고를 수 있다.

*#49 chose 16 blocks per module; 64 was a little faster on many-block workloads then, but installation and calls after it were not told apart. Here they were (figures in section 3.2 of the analysis). Confirmed: on `blocks`, 64 beats 16 in Chromium, Firefox and Node, and frames after every block is installed run some 14% faster too, so the number of live modules itself costs calls; the price is the first frame, each module taking longer to install; a working set beyond the module budget (batch sizes 1 and 4) runs slower than the interpreter even with the backoff at its cap, which the defaults avoid up to 65,000 blocks. Decision: the defaults (16 per module, a budget of 4,096) stay; a larger batch trades the first frame away, to be settled with measurements of consumer guests (phases 2A/2B) rather than a synthetic workload of some 24,000 blocks alone; consumers can already choose with `blocks_per_module`.*

## 후속 후보 / Follow-up candidates

- **인터프리터의 페이지 단위 무효화**: `smc`에서는 번역과 상관없이 인터프리터부터 느리다. 네이티브 Release에서 `alu`의 10~15 MIPS에 비해 1.8~2.7 MIPS다. 코드 페이지에 쓸 때마다 그 페이지의 디코드 캐시가 통째로 버려지기 때문이다(#34 결정 2). 쓰인 바이트가 디코드된 명령과 겹칠 때만 버리는 더 잘게 나눈 무효화가 후보다. 코드 옆에 변수를 두는 게스트에서 효과가 클 것으로 **추정**한다.

*The interpreter's per-page invalidation: on `smc` the interpreter itself is slow whatever the translation, 1.8 to 2.7 MIPS in native Release against `alu`'s 10 to 15, since every write to the code page drops that page's decode cache whole (#34 decision 2); finer invalidation, dropping only when the written bytes overlap decoded instructions, is a candidate, inferred to matter for guests keeping variables beside code.*

## 검증 / Verification

* 단위 테스트: 코드 변경 뒤의 억제(2배, 4배), 세는 동안 페이지가 바뀌면 다시 세기, 64배 상한, `invalidated_count`. 억제 때문에 바뀌는 기존 시험(대기열의 무효 블록, 비동기 묶음, 예산)의 기대를 고친다.
* 번역 강제 빌드(평가기, wasm)의 전체 ctest. 호스트 대조 fuzz의 `--translate` 재생은 사례마다 새 `Cpu`를 만들어 억제가 사례 사이로 넘어가지 않는다.
* 측정: `smc`, `alu`, `mixed`, `blocks`를 억제를 끈 빌드와 번갈아 잰다(네이티브 평가기, Node wasm).

*Unit tests: the backoff after code changes (2 and 4 times), the count starting over when the page changes while counting, the 64-fold cap and `invalidated_count`; existing tests the backoff changes (a stale queued block, asynchronous batches, the budget) get new expectations. Forced-translation builds (evaluator, wasm) run the whole ctest; the host-comparison fuzz's `--translate` replay makes a fresh `Cpu` per case, so no backoff crosses cases. Measurement: `smc`, `alu`, `mixed` and `blocks` alternately against a build with the damping off (native evaluator, Node wasm).*

## 소비자 영향 / Consumer impact

공개 계약에 `TranslationStats::blocks_invalidated`가 더해진다(필드 추가라 소스 호환). 번역이 켜진 소비자에서 코드 페이지에 자주 쓰는 게스트는 번역을 덜 시도한다. 결과는 인터프리터와 같다(README 목표 7).

*The public contract gains `TranslationStats::blocks_invalidated` (an added field, source compatible). With translation on, a guest writing its code pages often attempts fewer translations; results stay the interpreter's (README goal 7).*
