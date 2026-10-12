# #53 작업 로그 : v0.0.23 캠페인의 CMPS 판정과 하네스 세그폴트 / #53 work log : v0.0.23's campaign — the CMPS judgment and the harness segfault

이슈: [#53](https://github.com/reexec/rex86/issues/53) | 설계: [20261012-i053](../design/20261012-i053-campaign-cmps-and-segfault.md) | 지시서: [20261012-i053](../work-orders/20261012-i053-campaign-cmps-and-segfault.md)

## 2026-10-12

호스트: Intel Core i5-7200U(Kaby Lake, family 6 model 142), Windows 10의 WSL2 Ubuntu 24.04(GCC 13, multilib). 빌드는 캠페인과 같은 i386 Release(`-m32`, `-DREX86_WARNINGS_AS_ERRORS=ON`)다. 브랜치는 `work/i053-campaign-cmps-segfault`(main `1c8c8c0`에서)이다.

### 아티팩트

- [실행 38069279391](https://github.com/reexec/rex86/actions/runs/38069279391)의 `campaign-int-i386-*` 세 아티팩트를 받았다.
- 시드: v0.0.23의 오프셋은 2,300,000,000이다. 샤드마다 500만 건을 돌리고, 시드 간격은 1이다.
- 샤드별 러너와 결과:
  - warmup-default(AMD EPYC 9V45): 샤드 0, 1, 3에서 CMPS 불일치. `vendor_deviations`는 약 900(#40의 Zen 5 편차).
  - translate(AMD EPYC 9V45): 샤드 0에서 CMPS 불일치, 샤드 2는 종료 코드 139.
  - warmup-0(Intel Xeon 6973P-C): 샤드 0이 종료 코드 139. 148초에 끝났다. 다른 샤드는 566초가 걸렸으므로 약 26% 지점이다. 이 작업의 `vendor_deviations`는 0이다.
- 139로 끝난 두 샤드의 로그에는 명령줄 한 줄만 있다. `fuzz_campaign.sh`가 `2>&1`로 담으므로 stderr도 비어 있었다는 뜻이다. stdout은 파이프라서 블록 버퍼였고, 죽으면서 버퍼째 사라졌다.

### CMPS

- `rex86_trace --dump`로 다섯 사례를 보면 모두 같은 꼴이다.
  - 원본은 FS:(E)SI이고, FS는 null 선택자다.
  - EDI는 0x12FFD, 0x12FFF(cmpsd 넷), 0x12FFF(cmpsw)다.
  - 목적지의 첫 바이트는 작업 영역 안에, 나머지는 0x13000의 가드에 있다.
- 호스트(Zen 5)는 원본을 먼저 봐서 #GP를 낸다. 코어는 목적지를 먼저 봐서 0x13000의 #PF를 낸다.
- #40의 `SourceSegmentFirstCompareFault`는 `destination - kBase < kRegionSize`(시작 주소가 영역 안)이면 제외했다. 그래서 영역 끝에 걸친 목적지가 불일치로 남았다. 조건을 "첫 바이트와 마지막 바이트가 모두 영역 안이면 제외"로 바꿨다.
- 이 판정은 Zen 5 러너에서만 일어난다. 이 환경(Intel)에서는 정수 fuzz가 불일치 없이 도는지만 확인했다. 실제 효과는 다음 tag 캠페인에서 본다.

### 세그폴트

- **재현 시도**(500만 건씩):

  | 소스 | 구성 | 시드 | 결과 |
  |---|---|---|---|
  | 현재 main | warmup-0 | 2340000000 | 정상(불일치 0) |
  | 현재 main | translate | 2395000002 | 정상 |
  | v0.0.23 | warmup-0 | 2340000000 | 정상 |
  | v0.0.23 | translate | 2395000002 | **세그폴트(core dumped)**, 1회 |
  | v0.0.23 + 진단 | translate | 2395000002 | 정상 4회 |
  | v0.0.23 원본 fuzz | translate | 2395000002 | 정상 2회 |
  | v0.0.23 원본 fuzz, `setarch -R`(ASLR 끔) | translate | 2395000002 | 정상 2회 |

  v0.0.23의 translate 실행은 모두 9번 중 1번 죽었다. 같은 바이너리, 같은 시드가 다음에는 정상이었다.
- **입력과 무관함**: 생성기는 호스트 결과를 읽지 않으므로 입력 순서는 시드로 정해진다. 따라서 세그폴트는 입력만으로 정해지지 않는다. v0.0.24의 코어는 v0.0.23과 같다(차이는 `src/tools/bench`뿐). v0.0.24 캠페인의 int-i386 12샤드는 통과했으므로, 같은 코어로 CI에서는 24샤드 중 2샤드가 죽은 셈이다.
- **기각한 가설**:
  - REP가 명령 자신을 덮는 경우: scratch 사본에 "생성만 하고 실행하지 않는" 모드를 넣어 두 시드의 입력을 훑었다. 코드 페이지(0x10000~0x10FFF)에 닿는 REP는 translate 시드의 두 건뿐이었고, 둘 다 0x10000에서 DF=1로 아래로 써서 첫 반복에서 폴트가 난다. warmup-0 시드에는 하나도 없었다.
  - GS/TLS 오염: GS 접두사는 생성기의 무작위 접두사 목록에 없고, 세그먼트 적재는 `Allowed()`가 뺀다.
  - 시스템 호출: `INT`, `INT1`, `INT3`, `INTO`, `SYSENTER`는 `ExcludedMnemonic`에 있다.
  - 처리기 안의 #AC 재귀: POPFD만 300만 건 돌려도 재현되지 않았다. 역어셈블한 armed 경로 처리기는 스택 보호를 읽지 않고, 정렬된 저장을 한 뒤 `__longjmp_chk`로 나간다.
- **코어 메모리 결함 검사**:
  - 같은 500만 입력을 호스트 실행 없이 코어로만 재생하는 모드(scratch 사본, `REX86_CORE_ONLY`)를 ASan/UBSan i386 빌드로 돌렸다.
  - v0.0.23 코어와 현재 코어 모두 보고 0건이었다. 둘 다 `--translate` 구성이다.
  - 정상 하네스를 ASan으로 돌리면 첫 사례에서 멈췄다(신호 처리기에서 `siglongjmp`로 빠지는 구조와 ASan 런타임이 맞지 않음). 그래서 코어만 따로 검사했다.
- **결론(추정)**: 코어의 ASan으로 잡히는 메모리 결함은 아니다. 세그폴트는 하네스의 네이티브 실행 쪽(신호 전달, 커널, 러너 CPU)에서 드물게, 주소 배치에 따라 일어나는 현상으로 보인다. 메시지가 없으므로, 처리기가 메시지를 쓰기 전에 죽었거나 커널이 처리기 없이 끝냈다. 이 환경에서는 원인을 확정하지 못했다(**미확정**).

### 관측 수단(설계 결정 2)

- `rex86_int_fuzz`:
  - 줄 단위 stdout, `--progress N`(`progress case=… so_far=…`), `--from N`.
  - 사례 번호와 바이트를 빵부스러기로 기록한다.
  - 처리기 재진입을 감지하면 `nested signal in the handler`를 쓰고 `_exit(97)`로 끝낸다.
  - 게스트 바깥의 폴트는 `snprintf` 대신 비동기 신호에 안전한 출력으로 사례 번호와 바이트를 남긴다.
  - 진행 줄의 키는 `so_far=`이다. 캠페인 요약의 `mismatches=` 검색과 겹치지 않게 했다.
- `fuzz_campaign.sh`:
  - `ulimit -c unlimited`.
  - 신호로 죽은 샤드에 `died of signal N`을 쓰고, core와 gdb가 있으면 레지스터와 백트레이스를 그 샤드의 로그에 붙인다.
  - 가짜 명령(`kill -SEGV`)으로 시험했다: `exit=139`, `died of signal 11`, 마지막 진행 줄이 남는다.
- 워크플로 int-i386:
  - "Keep core files" 단계에서 `kernel.core_pattern`을 로그 디렉터리로 돌린다.
  - `--progress 250000`을 준다.
  - 실패하면 core가 있을 때 바이너리를 로그 디렉터리에 복사한다(아티팩트에 들어간다).
- 가이드 `integer-host-fuzz-and-traces.md`에 프로세스가 죽을 때의 절차를 더했다.

### 검증

- i386 Debug(현재 트리) ctest 9개 통과(정수 fuzz와 trace 묶음 포함). 정수 fuzz 10만 건 불일치 0, `--progress`와 `--from` 동작 확인.
- 캠페인 스크립트 시험(위)과 워크플로 YAML 구문(PyYAML).
- 문서: 분석 `integer-host-comparison.md`(#53 관측과 넓힌 판정 조건), 가이드, 설계, 지시서, 이 로그.
- 남은 것: 머지 뒤 patch 릴리스 tag 캠페인의 int-i386 결과. 다시 죽으면 새 관측 수단의 기록(진행 줄, 빵부스러기, core와 백트레이스)으로 이 이슈에서 이어 간다.

*Host: an Intel Core i5-7200U (Kaby Lake, family 6 model 142) under WSL2 Ubuntu 24.04 (GCC 13, multilib), builds matching the campaign's i386 Release, branch `work/i053-campaign-cmps-segfault` from main `1c8c8c0`. Artifacts: from run 38069279391 (seed offset 2,300,000,000, 5 million cases per shard, stride 1), the CMPS mismatches came from AMD EPYC 9V45 shards (about 900 vendor deviations each, #40's Zen 5 class), and the two exit-139 shards (9V45 translate shard 2; Xeon 6973P-C warmup-0 shard 0, dead at 148 s of 566 s) left only their command lines, stderr included, block-buffered stdout lost with the process. CMPS: all five traces have a null-FS source and an EDI of 0x12FFD to 0x12FFF, so the destination's first byte is in the work area and the rest in the guard; Zen 5 raises #GP on the source, the core #PF at 0x13000; #40's judgment excluded any destination starting inside the region, and now excludes only a destination whose first and last bytes are both inside; the judgment fires on Zen 5 runners only, so here only the absence of new mismatches is checked. Segfault: today's main ran both seeds clean, v0.0.23 crashed once on the translate seed (core dumped), then ran it clean four times with diagnostics, twice without, and twice without ASLR, 1 death in 9 translate runs, the warmup-0 seed never; inputs follow the seed alone, so the death is not decided by the input; v0.0.24 has v0.0.23's core (only `src/tools/bench` differs) and passed its 12 int-i386 shards, making 2 deaths in 24 CI shards. Rejected: a REP overwriting its instruction (a generate-only scan found code-page REPs only at 0x10000 going down, faulting at once), GS/TLS corruption (no GS prefix, no segment loads), system calls (excluded), #AC recursion in the handler (3 million POPFD cases clean; the armed handler path reads no canary and stores aligned before `__longjmp_chk`). Core memory errors: a core-only replay of the same five million inputs (no host run, `REX86_CORE_ONLY` in a scratch copy) under ASan/UBSan reported nothing for v0.0.23's core and today's, both with `--translate`; the normal harness hung under ASan at its first case (its signal-handler `siglongjmp` and the ASan runtime do not mix), hence the core alone. Conclusion (inferred): not an ASan-detectable core memory error, but a rare, layout-dependent event on the harness's native side (signal delivery, kernel, runner CPU) that killed the process before the handler could speak; the cause stays unresolved here. Observability added as in the design's decision 2: line-buffered stdout, `--progress`, `--from`, breadcrumbs, handler re-entry detection (`_exit(97)`), async-signal-safe reporting outside the guest and a `so_far=` key kept apart from the summary's `mismatches=`; `fuzz_campaign.sh` with `ulimit -c unlimited`, `died of signal N` and a gdb backtrace given a core, tested with a fake `kill -SEGV` command; the workflow's core_pattern step, `--progress 250000` and the binary copied beside a core; the guide's procedure for a dying process. Verification: i386 Debug ctest 9/9 (integer fuzz and corpora included), 100,000 integer fuzz cases clean with `--progress` and `--from` working, the campaign script test and the workflow's YAML; documents updated. Remaining: the patch release tag's int-i386 result after the merge; a recurrence continues here with the new records.*

## 2026-10-12 (후속: 원인 확정 / follow-up: cause found)

- **v0.0.26 tag 캠페인**([실행 38214393370](https://github.com/reexec/rex86/actions/runs/38214393370)):
  - 실패한 것: int-i386 translate와 warmup-default의 샤드 0이 signal 11로 죽었다. 러너는 둘 다 AMD EPYC 7763이다. 마지막 진행 줄은 125만과 375만이다.
  - 통과한 것: 나머지 10개 작업, warmup-0, 다른 샤드 전부.
  - CMPS 불일치는 없었다. 이번 러너가 Zen 5가 아니라 넓힌 판정이 쓰이지는 않았다.
  - `died of signal 11`은 나왔지만 러너에 gdb가 없어 백트레이스는 없었다. core와 바이너리는 아티팩트에 남았다.
- **core 분석**: WSL에 gdb가 없어(sudo 비밀번호 필요) scratch의 Python 스크립트로 ELF core를 읽었다. `NT_PRSTATUS`, `NT_SIGINFO`, `NT_FILE`과 `nm`으로 전역 주소를 찾았다.
  - translate: 사례 1,370,060, `BC D1 BC D2 57`(`mov esp, 0x57D2BCD1`), EIP 0x1002E, ESP = 힙 + 0x2BCD1.
  - warmup-default: 사례 3,765,805, `03 A7 95 83 3F 98`(`add esp, [edi+0x983F8395]`), EIP 0x1002F, ESP = 힙 + 0x2BD82.
  - 둘 다 `g_armed = 1`, `g_in_handler = 0`(처리기 미실행)이고, siginfo code는 128(SI_KERNEL)이다.
- **원인 확인**: scratch의 i386 프로그램(`altstack.c`)으로 확인했다. ESP를 malloc한 대체 스택 바닥 + 64에 두고 INT3을 내면, 기존 설정에서는 출력 없이 종료 코드 139로 끝난다. `SS_AUTODISARM`에 시험마다 다시 등록하면 세 번 모두 처리기가 돈다. 커널은 WSL2 6.18이다.
- **수정**: `Host`의 대체 스택에 `SS_AUTODISARM`(`1u << 31`, glibc 헤더에 없음)을 켰다. 등록에 실패하면 시작할 때 끝낸다. `Run`은 시험마다 `sigaltstack`을 다시 부른다. SIMD fuzz는 범용 레지스터 피연산자를 EAX로 제한하고 x87 fuzz는 신호를 쓰지 않으므로 바꾸지 않았다.
- **문서**: 설계 결정 3, kb `trap-flag-single-step.md`의 대체 신호 스택 항목.
- **검증**:
  - 수정한 fuzz로 v0.0.26에서 죽은 두 시드(2695000000 `--translate`, 2630000000)를 500만 건씩 돌렸다. 모두 불일치 0, 종료 0이다.
  - i386 Debug ctest 9개 통과.
  - 실제 효과(같은 꼴의 사례가 처리기를 거쳐 비교되는 것)는 ASLR에 달려 있어 로컬에서 정할 수 없다. 다음 tag 캠페인의 int-i386 결과로 본다.

*v0.0.26's tag campaign (run 38214393370) lost shard 0 of int-i386 translate and warmup-default to signal 11 on AMD EPYC 7763 runners (last progress at 1.25 and 3.75 million), every other job and shard passing and no CMPS mismatch appearing (no Zen 5 runner this time); `died of signal 11` was printed, but without gdb on the runner no backtrace, while the cores and binary reached the artifacts. With no gdb in WSL (sudo needs a password), a scratch Python script read the ELF cores (`NT_PRSTATUS`, `NT_SIGINFO`, `NT_FILE`, globals through `nm`): case 1,370,060 `BC D1 BC D2 57` (`mov esp, 0x57D2BCD1`, EIP 0x1002E, ESP at heap + 0x2BCD1) and case 3,765,805 `03 A7 95 83 3F 98` (`add esp, [edi+0x983F8395]`, EIP 0x1002F, ESP at heap + 0x2BD82), both with `g_armed = 1`, `g_in_handler = 0` (the handler never ran) and siginfo code 128 (SI_KERNEL). A scratch i386 program (`altstack.c`) confirmed the cause: ESP at the malloc'd alternate stack's bottom + 64 and an INT3 end the process silently with exit code 139, while `SS_AUTODISARM`, re-armed each time, runs the handler all three times (WSL2 kernel 6.18). Fix: `Host`'s alternate stack gets `SS_AUTODISARM` (`1u << 31`, missing from glibc's headers), failing at start if refused, and `Run` calls `sigaltstack` before every test; the SIMD fuzz (EAX-only general-purpose operands) and the x87 fuzz (no signals) stay as they are. Documents: design decision 3 and the alternate-stack note in the kb page `trap-flag-single-step.md`. Verification: the fixed fuzz ran both seeds that died in v0.0.26 (2695000000 with `--translate`, 2630000000) for 5 million cases each with zero mismatches and exit 0, and i386 Debug ctest passed 9/9; the real effect, such cases being compared through the handler, depends on ASLR and shows in the next tag campaign's int-i386.*
