# 대상 기판의 CPU / The CPUs of the target boards

근거 작업: [#21](https://github.com/reexec/rex86/issues/21) ([설계](../design/20261008-i021-target-board-cpu-baseline.md)) | CPU 기능의 기준: [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) Vol. 2A CPUID, [AMD K6-2](https://en.wikipedia.org/wiki/AMD_K6-2)

이 문서는 rex86의 최소 지원 사양을 정하는 네 기판의 CPU를 정리한다. 기판 사양은 제조사 공식 자료가 없어 공동체 자료로만 확인되고, 출처끼리 어긋나는 곳이 있다. 그래서 CPU 이름보다 **명령 집합의 상한**에 결론을 두었다. 출처가 어떤 CPU를 말하든 명령 집합의 결론은 같다.

*This document collects the CPUs of the four boards that set rex86's minimum supported specification. No manufacturer documentation exists, only community sources that sometimes disagree, so the conclusion rests on the **instruction-set ceiling** rather than the CPU name: whichever CPU a source names, the instruction-set conclusion is the same.*

> 확인 방법의 한계: 이 문서를 쓴 환경의 네트워크 정책이 원 문서 사이트(나무위키, arcadeotaku, gamerepair, ez2wiki, Shiz arcade-docs)를 막았다. 아래 기판 사양은 그 사이트들의 **검색 결과 발췌**와, 직접 읽은 MAME `ez2d.cpp` 드라이버 주석에서 왔다. 기판에 붙은 스펙표나 실기 덤프로 다시 확인할 수 있다.
>
> *Limit of verification: the network policy of the environment this was written in blocked the original sites (NamuWiki, Arcade Otaku, GameRepair, EZ2 Wiki, Shiz's arcade-docs). The board specifications below come from **search-result excerpts** of those sites and from the MAME `ez2d.cpp` driver comment, which was read directly. A spec label on a board or a dump from real hardware can confirm them.*

## 1. 기판별 CPU / CPU by board

| 기판 | CPU (출처) | 칩셋, 그래픽 | 게임 | 확실성 |
|---|---|---|---|---|
| 안다미로 MK3 | Pentium II 333 또는 366 MHz ([Arcade Otaku wiki][aow]). 실기 PC-Doctor 덤프는 Celeron 333 MHz([Shiz arcade-docs][shiz]), 갤러리 보고는 Mendocino Celeron([dcinside][dc]) | Intel 440BX 독자 보드, 3dfx Voodoo Banshee 온보드, SDRAM 64 MB, MS-DOS(DiskOnChip) + CD | rePIU의 DOS/4GW 게임(1st ~ PREX 3 MK3판) | CPU 계열(P6)은 일관, 정확한 모델은 출처마다 다름 |
| 안다미로 MK5 | Celeron 1.3 GHz, Tualatin 코어([나무위키 안다미로 Mk][namu-mk]) | VIA 694T 독자 보드(DCAMP), GeForce2 MX400 64 MB 온보드, SDRAM 128 MB, ESS ES1989S | Premiere 3, PREX 3(MK5판), Exceed(HDD, MK5 필수) | 출처 하나(교차 확인 없음) |
| EZ2DJ 1세대 | AMD K6-2 300 ~ 400 MHz(출처마다 클럭 표기가 다름)([나무위키 EZ2AC 기체][namu-ez2], [EZ2 Wiki][ez2wiki]) | Super Socket 7(VIA Apollo MVP3 또는 Soltek SL-56D5), Intel 740 8 MB, SDRAM 64 MB, Windows 98 | 1st Tracks, 1st Tracks SE | CPU 계열(K6-2)은 일관 |
| EZ2DJ 2세대 | **어긋남.** MAME `ez2d.cpp`는 EZ2Dancer 2nd Move(2001) 기판을 ASUS CUBX + Celeron 533 MHz로 적고 같은 HDD에 Socket 7 보드(FIC PA-2013) BIOS가 "원래 하드웨어?"로 남아 있다고 적는다([MAME ez2d.cpp][mame]). 다른 자료는 1st ~ 6th를 K6-2 기반으로 묶는다 | 2nd부터 RIVA TNT2 M64, 메모리 2배, Windows 98 SE([나무위키 EZ2AC 기체][namu-ez2]) | 2nd TraX 이후(어디까지인지 미확정) | **미확정**: Celeron 533(P6) 또는 K6-2 |

[aow]: https://wiki.arcadeotaku.com/w/Pump_it_UP
[shiz]: https://code.salty-salty-studios.com/Shiz/arcade-docs/src/branch/main/andamiro/board.md
[dc]: https://gall.dcinside.com/board/view/?id=superidea&no=212936
[namu-mk]: https://namu.wiki/w/%EC%95%88%EB%8B%A4%EB%AF%B8%EB%A1%9C%20Mk
[namu-ez2]: https://namu.wiki/w/EZ2AC%20%EC%8B%9C%EB%A6%AC%EC%A6%88/%EA%B8%B0%EC%B2%B4
[ez2wiki]: https://www.ez2wiki.com/EZ2_Arcade_System_Specifications
[mame]: https://github.com/mamedev/mame/blob/master/src/mame/misc/ez2d.cpp

*MK3: Pentium II 333 or 366 MHz on an Intel 440BX proprietary board with an onboard Voodoo Banshee (Arcade Otaku); a PC-Doctor dump from a real board reports a Celeron 333 (Shiz's arcade-docs), as does a gallery report (Mendocino Celeron): the family (P6) agrees, the exact model differs. MK5: a 1.3 GHz Tualatin Celeron on a VIA 694T proprietary board with an onboard GeForce2 MX400 (NamuWiki, one source); it runs Premiere 3, PREX 3 (MK5 builds) and Exceed. EZ2DJ generation 1: an AMD K6-2 at 300–400 MHz on Super Socket 7, the clock varying by source; 1st Tracks and 1st SE. EZ2DJ generation 2: **sources disagree**: MAME's `ez2d.cpp` describes the EZ2Dancer 2nd Move (2001) board as an ASUS CUBX with a Celeron 533 and notes a Socket 7 (FIC PA-2013) BIOS left on the same disk as "original HW?", while other sources group 1st through 6th as K6-2 based; unresolved between a Celeron 533 (P6) and a K6-2.*

## 2. CPU별 명령 집합 / Instruction sets by CPU

CPU 기능은 제조사 사양에서 확정된 사실이다(Intel SDM의 CPUID 기능 비트, AMD K6-2 자료).

| CPU | 정수 | x87 | MMX | SSE | 3DNow! | FXSAVE |
|---|---|---|---|---|---|---|
| Pentium II (Klamath, Deschutes) | P6(CMOVcc, CMPXCHG8B, UD2 등) | P6(FCOMI, FCMOVcc) | 예 | 아니오 | 아니오 | Deschutes부터 |
| Celeron (Mendocino, 333/533) | P6 | P6 | 예 | 아니오 | 아니오 | 예 |
| Celeron 533A (Coppermine-128) | P6 | P6 | 예 | **예** | 아니오 | 예 |
| Celeron (Tualatin, 1.3 GHz) | P6 | P6 | 예 | **예**(SSE2 없음) | 아니오 | 예 |
| AMD K6-2 | Pentium급(CMOV 없음, CMPXCHG8B 있음) | 387 계열(FCOMI/FCMOV 없음) | 예 | 아니오 | **예** | 아니오 |

* **합집합의 상한은 P6 정수 + P6 x87 + MMX + SSE(Pentium III의 Katmai SSE)다.** SSE는 MK5(Tualatin)가 요구한다. SSE2 이후는 네 기판 어디에도 없다.
* K6-2만의 3DNow!는 Intel 기판과 공유되지 않는 확장이다. EZ2DJ 1세대 게임이 직접 쓰는지는 census로만 알 수 있다.
* K6-2의 정수와 x87은 P6의 부분집합이므로 별도 구현이 필요 없다. 다만 K6-2 기판을 흉내 낼 때는 CMOV와 FCOMI/FCMOV가 #UD여야 하고, 이것은 CPUID와 기능 플래그의 일이다.

*CPU features are settled by the vendors (Intel SDM CPUID feature bits, AMD's K6-2). **The union's ceiling is P6 integer + P6 x87 + MMX + SSE (the Pentium III's Katmai SSE)**: SSE is required by the MK5 (Tualatin), and nothing past SSE exists on any of the four. 3DNow!, the K6-2's alone, is not shared with the Intel boards, and only a census can tell whether a generation-1 EZ2DJ game uses it. The K6-2's integer and x87 sets are subsets of the P6's, needing no separate implementation; emulating a K6-2 board means CMOV and FCOMI/FCMOV raise #UD, which is a matter of CPUID and the feature flags.*

## 3. 클럭 / Clocks

| 기판 | CPU 클럭 |
|---|---|
| EZ2DJ 1세대 | 300 ~ 400 MHz (K6-2) |
| MK3 | 333 ~ 366 MHz (Pentium II 또는 Celeron) |
| EZ2DJ 2세대 | 533 MHz (Celeron, 미확정) |
| MK5 | 1.3 GHz (Tualatin Celeron) |

실시간 실행의 상한 기준은 MK5다. 게임이 원래 기판의 CPU를 다 쓰는지는 게임마다 다르므로, 성능 목표의 수치는 벤치마크 하네스의 측정으로 정한다(README 목표 3).

*The real-time ceiling is the MK5. Whether a game used all of its board's CPU varies by game, so the performance numbers are fixed by measurements with the benchmark harness (README goal 3).*
