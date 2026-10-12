# #53 설계 : v0.0.23 캠페인의 CMPS 판정과 하네스 세그폴트 / #53 design : v0.0.23's campaign — the CMPS judgment and the harness segfault

이슈: [#53](https://github.com/reexec/rex86/issues/53) | 지시서: [20261012-i053](../work-orders/20261012-i053-campaign-cmps-and-segfault.md) | 로그: [20261012-i053](../work-logs/20261012-i053-campaign-cmps-and-segfault.md) | 근거: [정수 호스트 대조 분석](../analysis/integer-host-comparison.md), [#40 설계](20261010-i040-zen5-cmps-gp.md), [#37 설계](20261010-i037-tag-fuzz-campaign.md)

## 문제 / Problem

v0.0.23 tag의 캠페인([실행 38069279391](https://github.com/reexec/rex86/actions/runs/38069279391))에서 `int-i386` 세 작업이 실패했다. 아티팩트(샤드 로그와 실패 사례 trace)로 본 내용은 다음과 같다.

| 작업(러너) | 샤드 | 결과 |
|---|---|---|
| warmup-default(AMD EPYC 9V45) | 0, 1, 3 | `cmpsd` 불일치 4건, `cmpsw` 1건. 바이트 `64 A7`, `64 66 A7`, 호스트 #GP(9), 코어 #PF(1) |
| translate(AMD EPYC 9V45) | 0 | `cmpsd` 불일치 1건(같은 꼴) |
| translate(AMD EPYC 9V45) | 2 | 종료 코드 139, 로그에 명령줄 한 줄뿐 |
| warmup-0(Intel Xeon 6973P-C) | 0 | 종료 코드 139, 로그에 명령줄 한 줄뿐. 148초에 죽음(다른 샤드는 566초) |

*The v0.0.23 tag's campaign (run 38069279391) failed its three `int-i386` jobs. The artifacts (shard logs and failing-case traces) show five CMPS mismatches on the AMD EPYC 9V45 (`64 A7` and `64 66 A7`, host #GP, core #PF) and two shards that ended with exit code 139 and nothing but the command line in their logs, one on the 9V45 (translate) and one on an Intel Xeon 6973P-C (warmup-0, dead at 148 s of the others' 566 s).*

## 결정 1: CMPS — #40 판정의 누락 / Decision 1: CMPS — a gap in #40's judgment

실패 사례 trace를 `rex86_trace --dump`로 보면 다섯 모두 같은 꼴이다.
- 원본은 FS:(E)SI이고 FS는 null 선택자다. Zen 5는 원본을 먼저 보므로 #GP를 낸다.
- 목적지 ES:EDI는 0x12FFD~0x12FFF에서 시작하는 4바이트(또는 2바이트)다. 첫 바이트는 작업 영역 안에 있고 나머지는 0x13000의 가드에 있다.
- 코어(와 Intel, Zen 3)는 목적지를 먼저 보므로 0x13000의 #PF를 낸다.

이것은 #40이 벤더 편차로 분류한 바로 그 동작이다. 그런데 `SourceSegmentFirstCompareFault`는 "목적지 시작 주소가 영역 밖"일 때만 셌다. 그래서 영역 끝에 걸친 목적지가 빠졌다.

**결정**: 조건을 "목적지의 일부라도 영역 밖"(첫 바이트나 마지막 바이트가 영역 밖)으로 넓힌다. 나머지 조건은 그대로다. CMPS이고, 호스트가 #GP이고, 원본 세그먼트가 null 선택자이고, 호스트 기대값을 목적지 바이트의 #PF로 바꿔 읽으면 재생이 일치해야 한다. 코어의 폴트 주소는 처음 닿지 못하는 바이트(0x13000)이고, 기존 반복(목적지 + i)이 그 주소를 이미 시험한다.

*The failing traces (`rex86_trace --dump`) share one shape: the source is FS:(E)SI with FS a null selector, so Zen 5, reading the source first, raises #GP; the destination ES:EDI starts at 0x12FFD to 0x12FFF, its first byte in the work area and the rest in the guard at 0x13000, so the core (with Intel and Zen 3), reading the destination first, raises #PF at 0x13000. That is exactly #40's vendor deviation, but `SourceSegmentFirstCompareFault` counted only destinations starting outside the region. Decision: widen the condition to any part of the destination outside the region (its first or last byte), the rest unchanged (a CMPS, a host #GP, a null source selector, and a matching replay once the host's expectation reads as a #PF at a destination byte); the core's fault address is the first unreachable byte (0x13000), which the existing loop over destination + i already tries.*

## 결정 2: 세그폴트 — 원인 규명과 관측 / Decision 2: the segfault — investigation and observability

**확인한 것**(로그 20261012 절):
- 같은 시드(2340000000 warmup-0, 2395000002 translate)를 이 작업 환경(Intel Core i5-7200U, WSL2)에서 캠페인과 같은 i386 Release 빌드로 돌렸다. 현재 `main`(v0.0.25)으로는 두 시드 모두 정상 종료했다.
- v0.0.23 소스로는 translate 시드가 한 번 세그폴트(core dumped)로 끝났다. 그런데 같은 바이너리, 같은 시드를 다시 돌리면 정상 종료했다. v0.0.23의 translate 실행은 모두 9번 중 1번 죽었고, ASLR을 끈 2번은 둘 다 정상이었다. warmup-0 시드는 v0.0.23으로도 재현되지 않았다.
- v0.0.24의 코어는 v0.0.23과 같고(차이는 벤치마크 도구뿐), v0.0.24 캠페인의 int-i386 12샤드는 통과했다. 따라서 같은 코어에서 CI 기준 24샤드 중 2샤드가 죽었다.
- 입력은 시드로 정해지므로(생성기는 호스트 결과를 쓰지 않는다) **세그폴트는 입력만으로 정해지지 않는다.** 실행마다 달라지는 것은 주소 배치(ASLR, 힙)뿐이다.
- 하네스의 위험 경로를 점검해 기각한 가설:
  - REP가 명령 자신을 덮는 경우: 두 시드에서 코드 페이지에 닿는 REP는 0x10000에서 아래로 쓰는 것뿐이라 첫 반복에서 폴트가 난다.
  - GS/TLS 오염: GS 접두사와 세그먼트 적재는 생성기가 뺀다.
  - 시스템 호출: `INT`와 `SYSENTER`는 생성기가 뺀다.
  - 처리기 안의 #AC 재귀: POPFD만 300만 건 돌려도 재현되지 않았고, armed 경로의 처리기는 정렬된 접근만 한다.
- 메시지가 없다는 것도 단서다. stderr는 로그로 가므로, 게스트 바깥의 폴트였다면 처리기의 "outside the guest"가 남았어야 한다. 남지 않았으므로, 처리기가 그 전에 죽었거나 대체 스택이 넘쳐 커널이 처리기 없이 끝낸 것이다. 그런 경로의 한 예는 하네스 전역(`g_jump`, `g_armed`)이 덮인 경우다.
- 코어 쪽 메모리 결함을 확인하려고, 같은 500만 입력을 **호스트 실행 없이 코어로만** 재생하는 ASan/UBSan 빌드를 v0.0.23과 현재 코어로 돌렸다(결과는 로그).

**결정**: 원인을 이 환경에서 확정할 수 없으므로, 다음에 일어나면 원인이 남도록 관측 수단을 넣는다. 재현을 기다리며 tag를 막지 않는다.
1. `rex86_int_fuzz`: stdout을 줄 단위로 버퍼링한다(죽어도 출력이 남는다). `--progress N`은 N건마다 `progress case=… so_far=…`를 쓴다. 요약 키 `mismatches=`와 겹치지 않게 키 이름을 정했다. `--from N`은 앞의 N건을 생성만 하고 건너뛰므로, 죽은 지점 근처를 바로 재현할 수 있다.
2. 처리기: 실행 중인 사례 번호와 바이트를 전역에 기록한다(빵부스러기). 처리기가 다시 불리면(`SA_NODEFER`라 처리기 안의 신호는 재진입한다) "nested signal in the handler"와 빵부스러기를 비동기 신호에 안전한 `write`로 남기고 `_exit(97)`로 끝낸다. 게스트 바깥의 폴트도 `snprintf` 대신 같은 방식으로 사례 번호와 바이트를 남긴다.
3. 캠페인:
   - int-i386 작업은 `kernel.core_pattern`을 로그 디렉터리로 돌리고 `--progress 250000`을 준다. 실패하면 core 파일이 있을 때 바이너리를 함께 아티팩트에 담는다.
   - `fuzz_campaign.sh`는 신호로 죽은 샤드를 `died of signal N`으로 알리고, core와 gdb가 있으면 레지스터와 백트레이스를 그 샤드의 로그에 붙인다.

*Confirmed (log, 20261012): with the campaign's i386 Release builds on this workspace (Intel Core i5-7200U, WSL2), both seeds end normally on today's `main` (v0.0.25). With v0.0.23's source the translate seed once ended in a segfault (core dumped), yet the same binary and seed ran clean when repeated: 1 death in 9 translate runs of v0.0.23, both runs without ASLR clean, and the warmup-0 seed never died. v0.0.24's core equals v0.0.23's (only the benchmark tool differs), and its 12 int-i386 shards passed, so the same core died in 2 of 24 CI shards. Inputs follow the seed alone (the generator never reads host results), so the segfault is not decided by the input; what varies between runs is the address layout (ASLR, heap). Hypotheses checked and rejected: a REP overwriting its own instruction (the only REPs touching the code page start at 0x10000 going down and fault at once); GS/TLS corruption (GS prefixes and segment loads are excluded); system calls (`INT`, `SYSENTER` excluded); #AC recursion in the handler (3 million POPFD cases did not reproduce it, and the armed handler path makes aligned accesses only). The silence is itself evidence: stderr goes to the log, so a fault outside the guest would have left "outside the guest"; the handler died first, or the alternate stack overflowed and the kernel ended the process without it — as when the harness's globals (`g_jump`, `g_armed`) are overwritten. To test the core for a memory error, the same five million inputs were replayed on the core alone, without the host run, in ASan/UBSan builds of v0.0.23 and today's core (results in the log). Decision: since the cause cannot be settled here, add observability so the next occurrence leaves it, without holding tags for a reproduction: (1) `rex86_int_fuzz` line-buffers stdout, `--progress N` prints `progress case=… so_far=…` every N cases (a key distinct from the summary's `mismatches=`), and `--from N` generates and skips the first N cases to reproduce near a death; (2) the handler keeps the running case's number and bytes as breadcrumbs, reports a re-entry ("nested signal in the handler", SA_NODEFER making a signal inside the handler re-enter it) with the breadcrumbs through async-signal-safe `write` and `_exit(97)`, and reports a fault outside the guest the same way instead of `snprintf`; (3) the int-i386 jobs point `kernel.core_pattern` at the log directory and pass `--progress 250000`, keeping the binary in the artifact when a core exists, and `fuzz_campaign.sh` names a shard killed by a signal (`died of signal N`) and, with a core and gdb at hand, appends registers and a backtrace to that shard's log.*

## 검증 / Verification

* CMPS: 다섯 실패 사례의 꼴(원본 null FS, 목적지가 영역 끝에 걸침)을 trace 덤프로 확인한다. 판정 자체는 AMD Zen 5 러너에서만 일어나므로, 이 작업 환경(Intel)에서는 불일치를 만들지 않는다는 것만 확인한다. 실제 효과는 다음 tag 캠페인에서 본다.
* 관측 수단: `--progress`, `--from`의 동작, 신호로 죽는 샤드를 흉내 낸 `fuzz_campaign.sh` 실행, 워크플로 YAML 구문.
* 회귀: 정수 fuzz의 기본 실행과 ctest(i386).
* 세그폴트: 코어만 재생한 ASan/UBSan 실행 두 개의 결과. 다음 tag 캠페인의 int-i386이 녹색이면 이슈의 마지막 할 일을 마친 것으로 본다. 다시 죽으면 이번에 넣은 빵부스러기, 진행 줄, core로 이 이슈에 이어 간다.

*CMPS: the five failing cases' shape (null FS source, destination straddling the region's end) confirmed through trace dumps; the judgment only fires on AMD Zen 5 runners, so here (Intel) only the absence of new mismatches is checked, the real effect showing in the next tag campaign. Observability: `--progress` and `--from`, a `fuzz_campaign.sh` run with a shard faking a signal death, the workflow's YAML. Regression: the integer fuzz's default run and ctest (i386). Segfault: the two core-only ASan/UBSan runs; the issue's last item is done when the next tag campaign's int-i386 is green, and a recurrence continues here with the new breadcrumbs, progress lines and core.*

## 소비자 영향 / Consumer impact

없다. 바뀌는 것은 호스트 대조 fuzz(`tests/host/linux/`), 캠페인 스크립트, 워크플로다. 코어와 공개 계약은 그대로다.

*None: only the host-comparison fuzz (`tests/host/linux/`), the campaign script and the workflow change; the core and the public contract stay as they are.*

## 결정 3(후속): 세그폴트의 원인과 수정 / Decision 3 (follow-up): the segfault's cause and fix

v0.0.26 tag의 캠페인([실행 38214393370](https://github.com/reexec/rex86/actions/runs/38214393370))에서 int-i386 두 작업의 샤드 0이 다시 signal 11로 죽었다. 러너는 둘 다 AMD EPYC 7763이다. 결정 2의 관측 수단이 core와 바이너리를 남겼고, core를 Python으로 읽어(ELF 노트, NT_FILE, 바이너리의 심볼 표) 원인이 드러났다.

| 작업 | 사례(빵부스러기) | 명령 | 죽을 때의 ESP |
|---|---|---|---|
| translate | 1,370,060 | `BC D1 BC D2 57` = `mov esp, 0x57D2BCD1` | 힙 + 0x2BCD1 |
| warmup-default | 3,765,805 | `03 A7 95 83 3F 98` = `add esp, [edi+0x983F8395]` | 힙 + 0x2BD82 |

- 두 core 모두 `g_armed = 1`, `g_in_handler = 0`이다. 처리기가 한 번도 실행되지 않았다는 뜻이다. siginfo는 SI_KERNEL이다.
- 두 ESP는 각자의 힙에서 거의 같은 오프셋에 있다. `malloc`으로 잡은 64 KiB 대체 신호 스택의 바닥 근처다.
- **원인(확인됨)**: 시험 명령이 ESP를 대체 신호 스택 영역 안으로 옮겼다. 그러면 커널(`get_sigframe`)은 "이미 대체 스택 위에 있다"고 보고 스택을 바꾸지 않은 채 그 ESP 아래에 신호 프레임을 쌓는다. ESP가 바닥 근처라 프레임이 대체 스택 밖으로 넘쳤고, 커널은 처리기 없이 SIGSEGV로 프로세스를 끝냈다. 힙 주소는 ASLR로 실행마다 바뀌므로 시드로 재현되지 않았다.
- **재현(확인됨)**: scratch의 작은 i386 프로그램이 ESP를 대체 스택 바닥 64바이트 위에 두고 INT3을 낸다. 기존 설정에서는 처리기 없이 종료 코드 139로 끝났다(WSL2 커널 6.18). `SS_AUTODISARM`을 켜고 시험마다 다시 등록하면 세 번 모두 처리기가 돌았다.

**결정**:
- `rex86_int_fuzz`의 대체 스택에 `SS_AUTODISARM`을 켠다. 이 플래그면 `on_sig_stack()`이 거짓을 돌려 커널이 늘 대체 스택 꼭대기로 바꾼다. 커널 주석이 바로 "스택 포인터가 망가져 신호 스택 끝 가까이를 가리킬 때"를 위한 것이라고 적는다.
- 이 플래그는 전달 때 대체 스택을 해제하고 `sigreturn`에서만 되살린다. 하네스는 `siglongjmp`로 나오므로, `Host::Run`이 시험마다 `sigaltstack`을 다시 부른다.
- 플래그를 거절하는 커널(4.7 전)에서는 시작할 때 실패를 알리고 끝낸다.
- SIMD fuzz는 범용 레지스터 피연산자를 EAX로 제한하므로 게스트가 ESP를 바꾸지 못하고, x87 fuzz는 신호로 시험하지 않는다. 둘은 그대로 둔다.
- 결정 2의 관측 수단은 남긴다. 이번 원인을 찾은 것이 바로 그 core와 빵부스러기였다.

*v0.0.26's tag campaign (run 38214393370) lost shard 0 of two int-i386 jobs to signal 11 again, both on an AMD EPYC 7763; decision 2's observability kept the cores and binary, and reading the cores in Python (ELF notes, NT_FILE, the binary's symbols) showed the cause, as in the table above. Both cores have `g_armed = 1` and `g_in_handler = 0` (the handler never ran) and an SI_KERNEL siginfo, and both ESPs sit at nearly the same offset of their heaps, near the bottom of the 64 KiB alternate signal stack `malloc` placed there. Cause (confirmed): the tested instruction moved ESP into the alternate stack, so the kernel (`get_sigframe`) took the process to be on it already and built the frame below that ESP without switching; near the bottom the frame overflowed and the kernel killed the process with SIGSEGV without the handler, and since ASLR moves the heap, a seed did not reproduce it. Reproduction (confirmed): a small i386 program putting ESP 64 bytes above the alternate stack's bottom and raising INT3 dies with exit code 139 without its handler (WSL2 kernel 6.18), and with `SS_AUTODISARM`, re-armed each time, its handler runs all three times. Decision: give `rex86_int_fuzz`'s alternate stack `SS_AUTODISARM`, under which `on_sig_stack()` answers false and the kernel always switches to the stack's top (the kernel's comment names exactly this case of a stack pointer corrupted to near the end of the signal stack); the flag disarms the stack at delivery and only `sigreturn` re-arms it, so `Host::Run` calls `sigaltstack` again before every test, and a kernel refusing the flag (before 4.7) makes the fuzz fail at start. The SIMD fuzz limits general-purpose operands to EAX, so its guest cannot move ESP, and the x87 fuzz tests without signals; both stay as they are. Decision 2's observability stays: its core and breadcrumbs found this cause.*
