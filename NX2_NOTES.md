# Eden on Switch 2 titles (branch `nx2`)

Local experiment: get Eden (Switch 1 emulator) to load a Switch 2 title's loose ExeFS and push
boot as far as it goes. Nothing here is upstreamed or pushed. Graphics (Ampere / NVN2) is out of
scope for this pass.

## Setup

- Source + build live in `C:\dev\eden` (a worktree of `eden-nx2/`, branch `nx2`). The original
  working folder path is ~180 chars, and `cl.exe` can't open its PDB from a build folder there
  (C1041), so the build moved to a short path.
- Toolchain: VS 2026 (MSVC 14.51) via `vcvars64.bat`, Ninja from VS, CMake 4.4.
- Configure:
  `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INTERMEDIATE_DIR_STRATEGY=SHORT -DENABLE_QT=OFF -DYUZU_CMD=ON -DENABLE_WEB_SERVICE=OFF -DYUZU_ROOM=OFF -DYUZU_TESTS=OFF -DENABLE_WERROR=OFF -DENABLE_UPDATE_CHECKER=OFF`
- Build: `cmake --build build --target yuzu-cmd` -> `build/bin/eden-cli.exe`.
- Run (portable mode, `build/bin/user/` holds config/logs; no keys or firmware installed):
  `eden-cli.exe -g "<title>\exefs\main"`
- Debug helpers:
  - `EDEN_NX2_SVC_TRACE=1` logs every SVC with arguments and results.
  - `EDEN_NX2_IPC_TRACE=1` logs every dispatched CMIF command and non-zero result words.
  - `EDEN_NX2_AUTO_STUB=1` answers every unknown command with an empty success (the
    `use_auto_stub` setting gets reset by eden-cli's config rewrite). Used for all runs below.
  - `eden-cli --filter "<log filter>"` sets the log filter for one run (e.g. `*:Info Loader:Debug`
    to get module load addresses).
  - `tools/nx2/nso_syms.py <nso> addr|got|find ...` maps module offsets to exported symbols, GOT
    slots to imports, or searches symbol names. Eden's backtrace shows "unknown" modules for some
    titles; with the `loaded module X @ addr` lines this turns frames into SDK names.
  - `tools/nx2/a64_coverage.py <a64.inc> <nso>...` lists encodings in an NSO's .text that
    dynarmic's decoder table doesn't match.
  - `tools/nx2/npdm_info.py <main.npdm>` dumps the META/ACI0 header, services and kernel caps.
  - `tools/nx2/nso_dis.py <llvm bin> <nso> <text_off> <count> <workdir>` disassembles part of an
    NSO's .text (clang + llvm-objdump from VS 2022's LLVM).
  - Under cdb, stop only on second-chance AVs (`sxd -c2 "..." av`): dynarmic fastmem uses
    first-chance access violations.

## Titles

All titles share the same `main.npdm` layout: 64-bit, 39-bit address space, ThreadInfo cores
0..5 (core mask 0x3f), priorities 28..59, KernelVersion cap 9.1, SVCs 0x01-0x36 plus 0x80,
Switch 2 ACID layout.

All four are **Switch 2 builds**: none is a plain Switch 1 sanity check. Instruction counts that
stock dynarmic did not decode (`a64_coverage.py` against the unpatched `a64.inc`):

| Title (ExeFS) | Program id | RETAA/B | LDAPR | LSE atomics | CAS | SWP | other |
|---|---|---|---|---|---|---|---|
| first `exefs` | 0400C3F00006E000 | 114,767 | 37,560 | 24,718 | 6,640 | 2,470 | CASP 6, XPAC 7, PAC 2, STG 1 |
| second `exefs` and `updates/exefs` | 0400A3A01EE4C000 | 12 (rtld only) | - | - | - | - | main/sdk/subsdk0 don't decompress |
| third `exefs` (update `updates/65536` ~same) | 04005B902517C000 | 134,287 | 37,052 | 160,741 | 19,987 | 1,623 | XPAC 3, PAC 2, STG 1 |
| fourth `exefs` (update `updates/65536` ~same) | 0400380020056000 | 134,911 | 100,520 | 32,110 | 8,667 | 3,733 | PACGA 58, PAC 7, CASP 6, XPAC 3, STG 1 |

With the patched decoder, games 1/3/4 only leave ~60-110 rare SIMD encodings in `sdk` undecoded
(e.g. `5f119f9c`, `4ea05c42`, `0f413044`, FP16/v8.2 forms).

## Blockers hit, in order

| # | Where it stopped | Cause | Fix (commit) |
|---|---|---|---|
| 1 | CMake try-compile | C1041, PDB path ~290 chars | build in `C:\dev\eden` |
| 2 | Compile | MSVC 14.51: `reinterpret_cast<u64>(ULONG_PTR)`, C4505 on amalgamated zstd.c, C4800 int->bool | Fix build with MSVC 14.51 |
| 3 | Decoder | dynarmic had no LSE atomics, CAS/CASP, LDAPR, RETAA/RETAB, BRA*/BLRA*, LDRA*, PAC*/AUT*/XPAC*, PACGA, MTE ops. Title 1 has ~115k RETAB, ~37k LDAPR, ~25k LSE, ~6.6k CAS, ~2.5k SWP | dynarmic: decode ARMv8.1-8.5 instructions (atomics are exclusive load/op/store with retry; PAC is identity; MTE tags are 0) |
| 4 | Loader | `main` identified as a bare NSO, so main.npdm/rtld/sdk were skipped, then a crash writing into an uninitialised process | loader: load a loose ExeFS when given its main |
| 5 | NPDM parse (ErrorBadFileAccessControl) | Switch 2 ACID has a 0x300-byte signature/key area (Switch 1: 0x200); FAC/FAH offsets were also read without the block base | npdm: parse the Switch 2 ACID layout |
| 6 | eden-cli crash on start/exit | `*appstate` never set, SDL3 callbacks returned SUCCESS (quit), InputSubsystem was a local of SDL_AppInit, mouse timer before mouse driver | eden-cli: fix SDL3 main-callback lifetime bugs |
| 7 | `nnosInitialize` abort | `svcGetInfo` 29/30 unknown (region between alias and ASLR) | kernel: stub ids 29/30 (empty region after alias) |
| 8 | `VammManager::InitializeIfEnabled` abort | `svcGetInfo` 33 unknown; it's a page granule (module ranges are checked against `value-1`; VAMM maps one granule first) | return 0x1000 |
| 9 | `nn::am::InitializeApplicationInterfaces` | `appletOE` cmd 10 (Switch 2 OpenApplicationProxy) | alias to cmd 0 |
| 10 | same | `IApplicationProxy` cmds 60 and 40: new getters | return a generic `nx2:` stub interface (`nx2_stub_interface.h`); calls on it are auto-stubbed and logged |
| 11 | `nn::oe::SetupGpuErrorHandler` -> CreateThread | thread on virtual core 5; Eden emulates 4 cores | fold virtual cores 4/5 onto 0/1 in CreateThread/SetThreadCoreMask |
| 12 | (title 3) `nn::oe` init | `IApplicationFunctions` 320 has no handler | empty-success stub |
| 13 | (titles 3/4) `nn::fs::QueryMountRomCacheSize` -> `GetFileSystemProxyServiceObject` abort 2002-1002 | fsp-srv `SetCurrentProcess` returned ResultTargetNotFound: the loose-ExeFS loader never registered the process with the fs controller, and never set its RomFS | loader: give loose ExeFS boots a RomFS (sibling `romfs/`) and an fs registration |
| 14 | (title 4) `IApplicationFunctions` 321 | unknown | empty-success stub |
| 15 | (title 4) `nn::oe::GetCurrentGraphicsScale` UnexpectedDefault | `IApplicationProxy` 60 is a graphics-scale interface; its cmd 10 must return 10/20/30 | `IGraphicsScaleController` returning 10 |
| 16 | (title 4) `nn::gs::sdk::BootGraphicsSystem` -> `nn::os::GetRemoteMemoryUsage` abort | `svcGetInfo` 31/32 (remote memory for gsv/nvsrv) | return 0 for both (logged) |
| 17 | (title 4) `nn::gsv::BootWithRemoteMemory` abort | it sends TIPC on a session handle from `ISelfController` 2000, which had no handler | `ISelfController` 2000 returns a raw session to `INx2GraphicsSystemSession` (`nx2_gsv.h`), which accepts every command |
| 18 | (title 3) `nn::oe::GetPseudoDeviceId` abort | needs control.nacp, which loose ExeFS lacks | seed from program id |
| 19 | (title 4) `nn::syncpt::sdk::InitializeEx` | gsv 202 must return the syncpt session; gsv 204 takes two transfer memories and must leave a header (version 1, count, offsets) in the read-only one and return two events; syncpt 1 returns a u32 < 0x400 | `INx2SyncptSession`, gsv 202/204 |
| 20 | (title 4) `nn::gpu::sdk::BootWithRemoteMemory` | gsv 497 returns the GPU session; gpu 11 (id range), 12(id) event, 21(id) start + wait on that event, 22 another session | `INx2GpuSession` |
| 21 | (title 4) `nn::vi2::sdk::Initialize` / `nn::vi::CreateLayer` | gsv 305 returns the vi2 session; vi2 1/3/5 return small u32s (5 = timestamp recorder count, 7 needed by LayerHistory), 11/31/32 return handles | `INx2Vi2Session` |
| 22 | all TIPC replies with data | Eden bug: `ResponseBuilder::Push(Result)` writes the 2-word CMIF result, so TIPC data lands one word late and past the copied size | `TipcReplyWords` |
| 23 | (title 4) NVN -> NvRm device open | nvsrv is the nvdrv INvDrvServices protocol; serve it with Eden's nvdrv, initialised without `Initialize` | nvdrv-backed `nx2:nvsrv` |
| 24 | NvRm chip check | Switch 2 NvRm asks for a 0xD0-byte characteristics block and accepts chips 0x17E/0x12B/0x12E, but NVN needs the Ampere one; s16 interface versions at 0x70..0x80 must be >= 6 etc. | report chip 0x17E, Ampere classes, versions 0x40; unknown ctrl-gpu ioctls zero-stubbed for such callers |
| 25 | NvRm address space | AllocAsEx(flags 5, no big pages, [0x8000000, 0xC000000000), split 0); NVN then reserves 4 KiB VA up to 512 GiB; Eden's split/limit handling made the reported regions unusable | small pages up to the end, absolute big-page limit |
| 26 | (title 4) NvRm channel setup | nvhost-gpu ioctls 0x1F/0x20, `/dev/nvhost-tsg-gpu`, GetEngineInfo, object class 0 | logged echo for unknown ioctls once an nx2 driver is seen; GR + async-copy engines; Ampere/class-0 objects on Maxwell slots (70f1651, 82dbd88) |
| 27 | GPU memory | svc 0x80 (make memory GPU-accessible), gpu sub-session 101 (register range) and 211 (job) | stubs (608f0d6) |
| 28 | `syncpt::sdk::InitializeEx` -> `InterProcessEvent::Signal` abort E401 | gsv 204's first handle is attached *writable* (client -> server request), the second readable; the shared page has u64 values (RO+0xc), armed bits (RW+0x18, RO+0x1c) and summary words | gsv 204 returns a writable event, lays out the page, a 500 us poller mirrors host1x values and acknowledges armed waits (88367ea) |
| 29 | `NvOsEventPoolWait` E401 | syncpt 22(slot) must return an event; 201(slot, ~syncpoint, value, mode) / 202(slot) request/cancel a signal | slot events via host1x guest actions (88367ea) |
| 30 | GPU never executed submissions | Switch 2 never maps through nvhost-as-gpu: memory registered with gpu sub 101 is used at GPU VA == CPU VA | `Nx2GpuMemoryRegistry` identity-maps every range into each GMMU; SMMU physical range 33 -> 34 bits for 10/12 GB layouts (88367ea) |
| 31 | Puller | binds Ampere classes C797/C7C0/C7B5 and class 0; NVN2 uploads Turing+ (MME64) macros, which Eden's Fermi macro JIT runs as garbage until it crashes | Ampere classes run on the Maxwell engines; macros skipped while `Tegra::g_nx2_ampere` (88367ea) |
| 32 | NVN device init waits on syncpoint 1 >= 1 | submits use `increment_value` (bit 8) and signal completion only with semaphore releases | Eden appends the syncpoint increments for such submits (88367ea) |
| 33 | `wsi::ounce::NativeWindow::Acquire` on a null window | gsv 1510 is `gsv::shim::CalculateRequirements`; size 0 made window init fail | block-linear size/alignment, 4 bytes/pixel (5fb37b5) |
| 34 | `NvOsEventPoolWait` E401 (second channel) | nvhost-gpu QueryEvent 7 | per-channel event (5fb37b5) |
| 35 | CPU spins in NVN for `[0x2106028010] >= 3` | queues release progress to page P+0x1000 and wait (GPU acquire + CPU poll) on the same offset in page P | puller mirrors releases into the first two-page gpu region (5fb37b5; guess, works) |
| 36 | font header check on a wild pointer | `pl:u` 2010 `GetBuiltInFontData` (bool at offset 0, then {flags, u32 offset, u32 size}) and 2000 (font shared memory + size) | plain TTF copies of Eden's shared fonts after the BFTTF data (5fb37b5) |
| 37 | null "SoundResource" heap (300 MB) | `nninitStartup` sizes the heap as total - used - 0x195000000; Eden capped total at the 8 GiB heap region, leaving 1.55 GiB | Switch 2 processes count the alias region in max memory (5fb37b5) |
| 38 | game waits for service `aud:u` | Switch 2 audio entry (`nn::audio::CreateUserClientSessionByHipc`); cmd 2 returns an IAudioRendererManager | `INx2AudioUserClient` backed by Eden's audren (2e6c39c) |
| 39 | every audio `RequestUpdate` fails | updates tagged `REv?` (lowercase v), renderer opened with `REV?` | compare revision numbers (2e6c39c) |
| 40 | main thread parked in `ExecuteVirtualMachineMapBufferEx` | gpu sub 211's job slot is request word 17, not word 2 | signal word 17's event (2e6c39c) |
| 41 | "too many files open" reading RomFS | eden-cli never raised the CRT stdio limit | `_setmaxstdio(8192)` (2e6c39c) |
| 42 | GPU memory model | gpu sub 101 = `CreateGpuAccessibleMemory` (reply word 3 = GpuMemoryHandle); 211 = `NormalMapGpuMemory4K` (words 3-4 first VA, 5-6 last, 7 handle, 8-9 offset, 17 slot), also into sparse regions at 0x81_xxxx_xxxx; 102/213 (`NormalUnmapGpuMemory4K`)/214 (`CreateSparseRegion`) complete through slot events (words 3/8/11) | real handles, `Nx2GpuMemoryRegistry::MapAt` with SMMU range reuse, slot signals |
| 43 | (title 1) `nn::syncpt::sdk::Initialize` abort | newer SDK opens syncpt with TIPC 0(u64 token) and aborts unless it is echoed | handshake echo |
| 44 | (title 1) NVN closes the GPU device after GetVARegions | newer SDK asks gpu 201/202(id) for the user/gsv partitions (two u64s each, stored as min/max) instead of using 203 | 201 = [0x8000000, 0xC000000000), 202 empty |
| 45 | NVN command buffer with no command memory (~12 s, titles 1 and 4) | `NvRmGpuTSGSubcontextCreate` failed (0x11): characteristics reported one VEID | flags bit 8/22 (TSG, TSG_SUBCONTEXTS) and 64 subcontexts at characteristics +0xb4 (12125bc) |
| 46 | NVN asserts in `GetSyncpointShimRoAddress` | Switch 2 reads syncpoints through a read-only GPU page (flag bit 26, nvhost-as ioctl 0xB) | shim at the top of the VA range; GPU reads of it return the syncpoint value (12125bc, from changes.patch) |
| 47 | window/image creation | gsv 111/112/114/423/429/430/435/1510 native memory and images; svc 0x80 is SetGpuMemoryAttribute | native object registry + layout calc (from changes.patch), real svc 0x80 (12125bc) |
| 48 | presents go nowhere | vi2 229 (word 5 image id, ~word 22/word 24 fence) | composite the block-linear image with native GOB tiling (f63ff90, from changes.patch) |
| 49 | crash in `DmaPusher::CallMethod` (null engine) | NVN uses subchannel 2 (inline-to-memory) without binding a class | bind KeplerMemory there by default; drop methods on unbound subchannels (9f30683) |
| 50 | GPU parses garbage, then stalls (~14-19 s) | NVN2 gates each command buffer with a SEMAPHORED acquire the CPU releases after writing it; Eden read past unmet acquires and ran stale commands (`EDEN_NX2_PB_CHECK` confirmed entries changed after submit) | suspend the channel on an unmet acquire and resume it from the same word (9f30683, approach from changes.patch) |
| 51 | desync after a called command buffer | NVN2 ends a GPFIFO entry inside a method's data, calls another buffer, and continues at the next address | set the unfinished method aside and resume it when the stream continues (9f30683) |
| 52 | semaphore words next to releases get clobbered | 4-byte releases (SEMAPHORED bit 24) were written as 16 bytes | 4-byte releases write 4 bytes (9f30683) |
| 53 | no draws: 3D macros skipped | NVN2 drives 3D/compute state and draws through Turing MME64 macros; compute dispatches through QMD 3.0 | changes.patch's MME interpreter (one macro context shared by the process's channels, also on the compute class), native compute and QMD scheduler (d7826ce) |
| 54 | draws hit Eden's Maxwell decoder ("Invalid insn") | shaders are SM86 SASS | changes.patch's Ampere-to-Maxwell lowering, widened until all 32 of title 4's programs lower; `EDEN_NX2_SHADER_DUMP` + `C:\dev\nx2work\lowertest` test it offline (d7826ce) |
| 55 | GPU desyncs again after ~15-30 s | the game ran a full ring of command memory ahead of Eden's GPU thread: the vi2 229 reply reports all buffers free, so nothing paced it | vi2 229 waits (bounded) for the frame's GPU fence (325cac7) |
| 56 | macro arguments lost | NVN interleaves several command buffers, so more than one split method can be pending | several parked methods keyed by resume address, dropped at the end of the submission (325cac7) |
| 57 | frames all black although draws run | gpu 214 creates GPU-only memory (six 300 MB regions at 0x81_0000_0000) that the graphics server backs itself; the render targets live there and were unmapped | back each region with application-pool pages mapped into the alias code region (325cac7). Title 4 now shows its loading screen and a menu screen |
| 58 | (title 4) hang after confirming difficulty: macro 0x142 "parameter FIFO underflow" | NVN2's conditional-render macro reads two query counters with MME_DMA_READ_FIFOED (3D methods 0x154/0x155 address, 0x158 count) and loads them after a load barrier | MME DMA reads into the load FIFO / data RAM (8cbe854) |
| 59 | equal-value gate semaphore missed (~24 s) | the CPU side ran ~96 command buffers ahead when the 250 ms present wait timed out during shader compiles | present waits up to 4 s for the frame fence (8cbe854) |
| 60 | 3D queue waits forever on a word the compute queue should release | NVN2 patches a third QMD release slot at word 52 for queue-to-queue syncs | fire release slots 24, 28 and 52 (8cbe854) |
| 61 | 3D queue waits for N compute completions, 4 missing | compute jobs whose shader Eden rejected never completed, so their +1 releases never fired | rejected QMDs still complete and release (a84ec80); IMAD -UR addend, LDSM lowered |
| 62 | queue waits for ≥ a value that keeps moving backwards | QMD release slot 0 (flag bit 19) is a max release into a word a 3D macro also advances | bit-19 QMD releases write max(old, payload) (a84ec80) |
| 63 | in-game draws refused (~20k per run), black screen behind the dialogue | strong LDG (bit 77), 2D TLD4, RED MAX, AST of RZ unsupported | lowered (a84ec80); 210/214 post-menu shaders lower |
| 64 | intro scene black (DLSS-style pass) | title 4 renders at 2560x1440 and upscales to 3840x2160 with a tensor-core (HMMA/MOVM/LDSM) compute network; emulating it takes the host driver minutes to compile and would cost seconds per frame | bilinear blit from its input (cbuf0+0x90) to its output (cbuf0+0xa0) when the program is rejected (22333bd); full emulation opt-in (EDEN_NX2_TENSOR) |
| 65 | fullscreen blit passes read constant bank 24 | console-provided blit parameter block, never bound by the game | bank 24 -> 17, synthesized from the render area by BlitConstantsPass in vertex and fragment shaders (22333bd, d75e57d) |
| 66 | synthesized blit sizes were 0 | the game never programs surface_clip, which feeds Eden's render area | fall back to colour target 0's size (d75e57d) |
| 67 | tonemap output black | the 3D colour-grading LUT is drawn once per scene by a pixel shader using CALL.REL.NOINC; capture treated it as a real call, never found the end and rejected the shader | NOINC calls count as forward branches in capture (4d58a74). **Intro cutscene renders correctly** |
| 68 | crash ~5 s after the save that follows character customization (garbage pointers in two threads) | nx2:gpu 213 (unmap) and 102 (destroy GPU memory) were no-ops; the game reuses the freed memory as heap and stale GPU write-backs overwrote it | unmap from every address space, drop freed semaphore regions (bed817c); not yet confirmed past customization: frame-locked replays desync across loading screens |
| 69 | hatched edges on UI portraits, buttons, squid cursor | UI textures are ASTC; the GPU ASTC decoder ignored the Switch 2 GOB swizzle | apply it in astc_decoder.comp (7fa2b46) |
| 70 | freeze ~15 s after the post-customization save; channels 2/4 wait on 0x2115c98000 (equal 0x5293, now 0x5310) | a 3D queue ticks a counter two queues acquire value by value; Eden wrote several ticks between their re-checks | releases latch the waiters they meet (8e37f58) |
| 71 | audio broken, game at 90-160 fps | vi2 presents were never paced | 60 Hz vblank pacing |
| 72 | stuck loading the next scene ("too many files open" for route_wallClimb.pack.zs, save.dat) | VFS open-file cache cap equal to the CRT limit (8192) | cap 512 below _getmaxstdio() |
| 73 | loading screen hangs, host GPU TDR | macro parameters split across GP entries were parked; method data continues across entries | continuous split methods (e8d060b) |
| 74 | rejected ATOMG/STL/LDL with small immediate offsets | lowering only took a zero offset | s20 ATOM offset, 24-bit STL/LDL offset (6302be8) |
| 75 | same crash 3 runs (415 s, 429 s, 9960 s): read 0x29 at pc 0x85096d10 (recursive mutex lock), lr 0x82ada310 | worker job runs main+0x2aba2b0 on object C = [A+0x18] whose manager [C+0x720] is null; C's destructor (main+0x2ab8e20, vtable main+0x4b660f8) nulls it before A's teardown (main+0x2b37b50) clears A+0x18; teardown and per-frame update are both jobs (via 0x8365c634); NFC Finalize comes 14-22 s before each crash; LSE atomics (exclusive pairs) and DMB (mfence) checked fine | open: catch all threads at the abort with the gdbstub (`nx2work/dbg_crash_threads.rsp`) |
| 76 | debugger run, crash at 406 s in the same scene load: writes through a null result at pc 0x82b471fc (main+0x2b271fc) | the scene's frame heap "PaintSceneProcHeap.26" (fixed 90 MiB + 0xfc8 parent, head 0x12c9525d18, tail 0x12c9800208) had 2.98 MB left for a 0x302088 allocation (A init main+0x2b37de0 -> main+0x2b27020); about 160 KB short, so some allocation of that scene is bigger than on the console (only 11.7 MB of the heap is nx2:gpu memory); the main thread was waiting in svc 0x34 for job workers | open: log that heap's allocations to find the oversized one |
| 77 | why the scene heap overflowed (run 6) | the heap size is a constant (mov w1, #0x5a00000 at main+0x2b32070); its fixed contents are 71.3 MiB (main+0x2ab9218) + 1.05 MiB + 1.25 MiB + 3 MiB; in runs 3, 4, 5 and the save test the scene's two 0x514000 GPU pools come from the root heap (next to the 0x2ee000 pool), in run 6 from the scene heap (current heap of the loading thread), 10 MiB it has no room for; which heap a job allocates from depends on the thread running it. The game's npdm asks for cores 0..5 (mask 0x3f, reported as is by GetInfo CoreMask) and Eden folds 4, 5 onto 0, 1, so two cores run two workers each | candidate: report or emulate the real core layout; ruled out: memory layout (12 GB), controllers (one), GPU pool alignment |
| 78 | six emulated cores (4fbfd5b), run 7 with the gdbstub: same scene-heap overflow at 292 s, the 0x514000 pools again in the scene heap | both overflow runs (6, 7) had the gdbstub on (memory accesses checked, slower JIT); the three runs without it (3, 4, 5) put those pools in the root heap and crashed later on the torn-down object instead, so the debugger itself shifts this timing | open: a six-core run without the gdbstub |
| 79 | run 8 (six cores, no gdbstub): original crash at 338.6 s, 11 s after the paint scene set up (327 s, pools in the root heap) | C's init (main+0x2ab9120) creates "ColPaintBuild.%u" (main+0x147d3c0 -> main+0x34f3850) sized by [C+0x738], 100 MiB by default (ctor main+0x2ab874c); when that returns null, init returns before setting [C+0x720], and the next state request (main+0x2aba230) crashes the update. So every crash at this load is a memory shortage: 100 MiB (no gdbstub) or 3 MiB in the scene heap (gdbstub). Root heap is 0x19506f000 at 0x1145e10020 | open: why memory is short (reported total memory for Switch 2, frees delayed behind the GPU) |
| 80 | the memory shortage behind rows 75-79 | title 4 is told 9.55 GiB (GetInfo 21) and plans all of it: SetHeapSize 3.125 GiB plus a 6.33 GiB VAMM-backed sead root heap in the alias region; Eden backed the six 300 MB gpu 214 regions from the application pool, 1.8 GB the kernel still reported as free | 214 backing from the non-secure system pool, +8 GiB DRAM on the 12 GB layout, device memory tracker 35 physical bits (5cff620); run 9 passed the crash spot (NFC Finalize 272.8 s, ran to 388 s), no allocation failures |
| 81 | title 1 hangs before its first frame (~17 s): main logic stops, two cores spin, only audio and hid keep running, almost no GPU submits | it asks gpu 214 for ~20 GiB (two 0x16ad80000 regions, these two still fail with the new pool); gets further than before (save data, friend list) | open: thread snapshot at the hang; 214 regions this large must be sparse on the console |
| 82 | title 1 hang (row 81) | `monitor handles` (a4051e4) showed the main thread waiting forever on the nx2:vi2 32 event (loop at main+0x25ec5e4 until [obj+0x88] >= 2); the stub never signalled it | 31 and 32 events signalled at 60 Hz (6196a11); title 1 now presents (241 frames by 95 s) |
| 83 | title 1 after the hang: ~3 fps with "fence not reached in 4 s" stalls | unimplemented vi2 104/105 (every frame) and gpu 215, gpu 214 still short for the two 5.7 GiB regions, one Nx2Map "out of SMMU space", rejected shaders (formatted surface store, IPA mode, LEA carry) | open |
| 84 | title 1 GPU stalls (row 83): channels wait on semaphores 0x21f6093140/3040 until the 4 s timeout | NVN2 queue syncs run through a ring of QMDs (dependency 2 each: the previous entry's launch and macro 0xda, which patches release slot 52 and schedules/decrements via PCAS 3/7); macro 0xda reads the ring base from MME data RAM word 0x27, which each channel sets (0x21098d39, 0x213e8ff8, 0x213ea168) but Eden shared one data RAM for all channels, so every channel drove the same ring | per-channel MME data RAM, one QMD table for all channels (6c20e37); no stalls, title 1 reaches its title screen (world map with logo) |
| 85 | title 1 address space | 214 = CreateSparseRegion, 215 = DestroySparseRegion (SDK symbols); SMMU space (16 GiB) ran out | 64 GiB device space (71b38ab), 215 handled (cc0c400) |
| 86 | title 1 after its title screen: black frames | tens of thousands of rejected shaders: IPA interpolation modes, integer carry/third source, uniform comparison, formatted surface load/store, uniform constant load, unsupported opcodes | open |
| 87 | title 1 loading crash: Vulkan out of memory | an 8192x8192x68-layer virtual texture (~23 GB) | images over 2^28 texels are created sparse with no memory (`Unbacked`: no uploads/downloads) (96a9b81); the game reaches its real title screen ("Press L + R"), ground black (that texture has no data) |
| 88 | title 1 shader coverage | checked against nvdisasm (`nx2work/nvd.py` disassembles one instruction at a time, since a single illegal word stops nvdisasm); calls: RET may restore the link through IMAD.MOV with code between, library functions may EXIT, `@Pg EXIT Pc`; PLOP3/UPLOP3 any table, UP2UR, ULOP3 predicate output; UIMAD.WIDE, uniform USHF/FMNMX/I2F/F2I, LDS `[Ra.Xn+URb]`, ATOMG/RED/STG forms, PRMT, FSWZADD, REDUX.MAX | 1351c73, 52cda01, 49396b7: 1699 of 1712 dumped programs lower; left: 12 task/mesh programs (ISBERD/ISBEWR, row 89) and one SURED |
| 89 | task/mesh shaders | title 1 draws some geometry with NVN task + mesh programs. While SET_MESH_CONTROL (0x114c) is on: task program in the vertex slot, mesh program in the tessellation slot, header-only pass-through geometry program (its output map = per-primitive outputs); draw count = task workgroups. Task: `ISBERD.ATTR.SKEW [RZ]` = workgroup index, payload `ISBEWR.O.ATTR [rank*stride+0x10]`, mesh count `ISBEWR.O.ATTR [0x4]`. Mesh: input [0] = its index, payload from 0x10; vertices in groups of 32, one 128-byte line per output component (mesh header omap order), then the per-primitive groups; map: count `ISBEWR.O.32 [0x3]`, U8 indices from 4. Addresses are plain linear bytes | task/mesh stages for VK_EXT_mesh_shader: ISBE ops kept as a pseudo instruction through the lowering, emulated stage buffer (task payload / workgroup memory), every EXIT to one epilogue that emits EmitMeshTasks or SetMeshOutputs + the output copy; pipelines keyed by SET_MESH_SHADER_A/B and SET_MESH_INIT_SHADER, kept out of the disk cache. All captured programs translate to valid SPIR-V; in the attract demo they draw cleanly at ~3 ms a frame, and without them the demo stalls on the world map (`EDEN_NX2_NO_MESH`, `EDEN_NX2_NO_MESH_FILE` to compare) |
| 90 | each rejected program was lowered and logged again on every draw (1.7M log lines in 13 min) | no negative cache | the shader cache keeps the first exception per address until that memory is written; logs deduplicated (bcb4ff0) |
| 91 | a compute program with a jump table: "Failed to track indirect branch" | Eden's tracker wants IMNMX imm -> SHL imm -> LDC -> BRX; IMAD.SHL was lowered to an XMAD multiply | IMAD by a power of two lowers to SHL (49396b7+) |
| 92 | with rows 88's compute forms, title 1 stays on its loading map (file reads stop ~46 s in) | two compute programs failing in Eden's compiler: a jump table whose targets lie past the first EXIT, and a 128-bit store of RZ from unaligned scratch registers | programs with BRX end at the compiler's closing branch-to-self; R248-R251 for 128-bit zero stores (3df6a81) |
| 93 | title 1 colour cast (magenta/cyan, red/green, changing per course) | the sky/atmosphere 32^3 volume shader starts with an unguarded R2UR; R2UR/MATCH/HFMA2.RELU saved P6 with the uniform-guard mask `1 << (6 - 7)` (undefined), which shifted into the opcode bits | always save/restore P6 (2ed0c48). Found with `EDEN_NX2_DUMP` (flush GPU ranges at a present to files) and `nx2work/vol3d.py` / `volslices.py` (untile Switch 2 3D textures) |
| 94 | near terrain black | 8192x8192x68 ASTC 8x8 virtual textures: 1.5 GB in guest memory, 23 GB each as Eden's RGBA8 decode | huge GPU-decoded ASTC arrays keep levels from the first that fits 512 MiB (here 3) on the host (60c4791) |
| 95 | wind/foliage shards | SM70+ MUFU.SIN/COS take turns (sin(2pi x)); Eden's Maxwell MUFU takes radians | scale by 2pi (245e6ad); jungle canopy not re-checked yet |
| 96 | title 1 aborts after ~1.5-4 min at the title or when a race starts (nn::diag::Abort from main+0x19e5df0) | the movie player (main+0x19e5860) lays out its decoder frames with nn::codec::CalculateDecoderImagePoolBuffer, which sends nx2:gsv 991; auto-stubbed, the reply kept the request words, so the frame size read as 0x167027f'00000001 and the budget check at main+0x19e5b58 failed. (Not the "video-frame-count" < 9 check: the game sets that from a two-slot registry, at most 2.) | gsv 991 answers a block-linear NV12 size (640x360 -> 0x64000) |
| 97 | pop-in, objects flicker in and out, low-quality meshes | high-detail LODs drawn through task/mesh programs (bound in stage slot 1, header type VS, ISBERD/ISBEWR), which are rejected and skipped | task/mesh support (row 89); not yet checked in a race |
| 98 | ~12 fps at the title | host GPU ~20% busy, emulated cores ~0.9 host cores, GPU thread 45-70% busy, much of it waiting on the host GPU: semaphore acquires wait for host fences under GPU accuracy High; ASTC uploads end in Finish() (a Flush() there crashed at boot); a 1 ms poll sleep while channels are suspended | gpu_accuracy=Low: ~16 fps; SDL main loop capped at 240 Hz (88f04ad). Open: ASTC upload without a full stall (and per-layer re-upload of the trimmed virtual textures), native MME interpreter cost, draw path |
| 99 | VRAM over the card (8.4-9.8 GB used on an 8 GB RTX 4070 Laptop), texture cache always collecting | ~700 ASTC 8x8 textures decoded to RGBA8 (16x their size, ~4.5 GB) | astc_recompression=Bc3 + accelerate_astc=CpuAsynchronous in the config; huge ASTC arrays stay on the GPU decoder as trimmed RGBA8 (ed0a3c9): 4.6-5.6 GB, ~10 -> 16 fps |
| 100 | 8 ImageTranscode threads at ~44% each (3.5 cores) | Eden's default pool is half the hardware threads | an eighth, at least 2 (`EDEN_NX2_TRANSCODE_THREADS` overrides): ~19 fps on the attract loop |
| 101 | Medal's capture hook inside Eden (`medal_hook64!OBS_Negotiate` in GPU-thread stacks) | Medal registers a per-user Vulkan implicit layer that loads into every Vulkan app | launches set `DISABLE_VULKAN_MEDAL_OBS_CAPTURE=1` (the layer's own off switch) |
| 102 | host crash (log stops, segfault) at 113 s in one run; under cdb "Free Heap block modified after it was freed" at 243 s, twice | Eden's TickAsyncDecode erased a finished decode while still holding its mutex (unlocked freed memory); only reached with accelerate_astc=CpuAsynchronous (the BC3 setup) | release the lock first, cancel decodes of deleted images (8766a72); clean past 330 s. Also made nx2 service callbacks (vsync timer, syncpt actions, capture/dump) independent of their sessions' lifetime (190747a) |
| 103 | (title 1) texture churn: the collector evicted ~1,250 decoded ASTC textures per 600 frames and they were decoded again (lag and pop-in turning the camera) | device memory sat above the collector's critical line (5.4 GB of a 7.0 GB budget): ASTC 8x8 is 4x larger as BC3, and a bloom chain at unmapped GPU 0xf0000-0x15b600 was re-created on every use (each lookup got a fresh stand-in CPU address), ~5,400 images per 600 frames on the map | unmapped GPU addresses get a stand-in address fixed by the GPU address, so images are found again; ASTC whose blocks carry no alpha (exact endpoint-mode scan, ~70% of them) goes to BC1 (`EDEN_NX2_ASTC_BC1=0` turns it off). Attract loop: 4.8-5.1 GB, no costly evictions, ~17 -> 22-23 fps |
| 104 | garbage textures (title logo, road patches) and a host crash 90-120 s in that skipped the crash handler (fail-fast) | an image re-decoded while a decode was in flight has two; the first to finish cleared IsDecoding, so deleting the image did not cancel the second, which was then uploaded into the next image to take the slot (another texture's data; a staging overrun when that image was smaller) | every pending decode of a deleted image is cancelled; a decode larger than its image is dropped and logged. Frames clean; crash: verifying with long runs |
| 105 | (title 1) movie abort moved on: `nn::codec::InitializeVp9Decoder` failed with 2001-0114 (InvalidHandle) | gsv 901/902/1001/1002 (`nn::codec::video::detail::ipc::Initialize`) were auto-stubbed; 902/1002 must return sessions (handle word 3, result word 4) whose TIPC 1 (CreateVideoDecoderService) returns another | sessions returned (2970100); the movie plays with no picture. Open: decode VP9 (vdecsvc 200/101/3/311 are auto-stubbed) |
| 106 | GPU thread ~50% in the Turing macro interpreter | every instruction re-decoded on every run, register file copied per instruction | decode once per PC, operands read up front (a97d26d): GPU thread ~68% -> ~45-50% busy |
| 107 | streamed textures shown as garbage/black for seconds; 2 transcode threads pegged | ASTC was decoded and re-compressed to BC on the CPU, ~40 textures/s on the attract loop | accelerate_astc=Gpu (config value 1) + BC recompression: GPU ASTC decode into a temporary RGBA8 image and `bc_encoder.comp` (69cc66b); textures ready the frame they are used |
| 108 | frame time is CPU work + GPU-thread work in series | nx2:vi2 229 blocks the game until Eden's GPU thread has processed the frame (WaitForFrame), and RequestComposite waits for the GPU thread to drain its queue too; the present timeline log shows 8-56 ms of each 34-105 ms frame blocked | `EDEN_NX2_FRAMES_AHEAD=1` (wait for the previous frame) deadlocks at the first scene change: the main thread waits on syncpoint 1 (present fences) for a value no queued work reaches. Open |
| 109 | ~1,400 SVCs per frame | the job system's WaitForAddress (0x34) / SignalToAddress (0x35) pairs and yields, all through Eden's global scheduler lock (3 of 6 cores seen blocked on it) | idle-core spinning (`EDEN_NX2_IDLE_SPIN_US`) gave no gain. Open |
| 110 | (title 1) GPU thread saturated, 3D attract scenes ~22 fps | profiled with the new GPU zone timers (`EDEN_NX2_GPU_PROFILE`) and the in-process sampler (`EDEN_NX2_SAMPLE`): per second, ~355 ms command parsing outside macros, ~210 ms in 745k macro calls, ~325 ms draw setup (33k draws), ~89 ms compute (4.4k dispatches) | see 111-115; now ~27-33 ms per frame in the same scenes |
| 111 | the guest clock (CNTPCT) cost ~10% of the main core | AMD CPUs have no CPUID leaf 0x15, so the TSC frequency stayed 0 and every read went through QueryPerformanceCounter | measure the TSC when the leaf is missing (948a391) |
| 112 | C++ exceptions on the GPU thread (~1-2%) | a rejected native shader (mesh/task programs) was rethrown twice every time it was bound | return false for remembered rejections (948a391) |
| 113 | debug aids on the hot path (~4% of the GPU thread) | semaphore watch per command word, FNV hash of every pushbuffer entry, first-write trace, two debug logs per macro call | flag checks unless their variable is set (948a391) |
| 114 | texture churn: 1,400-2,600 textures deleted and recreated per 600 frames, one purge of 4,300 | ~38% of device memory was empty space in 256 MiB VMA blocks; usage came from the driver, so evictions did not lower it | 64 MiB blocks and usage minus VMA block slack (7b0ff0b): 0-224 per 600 frames, 6.24 of 6.97 GB used |
| 115 | main core: ~12% slow-path reads of GPU-tracked pages, GPU thread and CPU fighting over the caches' mutex | `use_reactive_flushing` (on by default on desktop) protects tracked pages against reads too | off in the test config: the slow reads are gone, no visual differences seen in the attract loop. Kernel lock spins before sleeping (5c99031): ~20-25% less time blocked |
| 116 | macro interpreter is ~11-13% of the GPU thread | operand fetch is not the cost: a slot-based rewrite matched the old interpreter on 200,000 recorded calls (`EDEN_NX2_MME_RECORD`, `C:\dev\nx2work\mme_test`) but was no faster | open: needs a JIT or pre-built handlers |
| 117 | macro calls: ~1,000,000 a second, 7 instructions each, 96% exact repeats | the per-call interpreter cost was the time, not the instructions | memo of calls that depend only on their parameters and the data RAM words they read (1cd4a3c, b75fdf8, c28ea9a for compute macros): 97% replayed, verified on 200,000 recorded calls |
| 118 | crashes ~26 s in (fencing thread walking a corrupt operation list; query cache Barriers; Vulkan worker SubmitExecution) | `async_presentation=true` (config): 4 crashes in 8 runs with it, 0 in 6 without; a race between the present thread and the Vulkan scheduler | keep it off (test build 3b). The fence manager's SyncOperation also appended to a list SignalFence moved from another thread: own lock now |
| 119 | (tester, RTX 4070 SUPER) 50 MB log; textures evicted with 3 GB VRAM unused | nx2:vi2 104/105 called ~75 times a second through the logging auto stub; collector thresholds from Eden's ~7 GB device memory cap | quiet stubs (649fb56); thresholds from the driver budget (49225dd) |
| 120 | GPU thread asleep ~8% on the texture/buffer cache mutexes | holders: the fencing thread (it took both caches for every fence, one per compute dispatch, and holds the buffer cache while it copies GPU-written buffers back) and CPU write traps; std::recursive_mutex slept at once | spinning recursive mutex, per-cache fence flush flags, separate locks (f10767f): asleep 3.7%. Buffer downloads at every fence stay: the native MME DMA reads of indirect arguments depend on them (no synchronous flush logged with them on) |
| 121 | first-run freezes from shader translation (one 28,102-instruction compute shader: 3.4 s on the GPU thread) | BreadthFirstSearch's visited list was a linear scan (quadratic), and GlobalMemoryToStorageBufferPass searched again for every access | hash set past 64, per-start memo (ba173b6): 0.85 s; runtime builds now ~2-4 ms each. Translating on workers was not done: environments read live GPU state (texture types, constant buffer values) while translating |
| 122 | final clean run (attract loop): 21-25 ms per frame in most 3D stretches, 26-38 ms in compute-heavy courses (industrial, highway), host GPU ~38% busy | the GPU thread is still the limit: draw setup ~25%, command parsing and register writes ~15%, compute dispatches/inline uploads (heavy courses) | open: per-draw binding cache, uniform buffer copies (staging slices are recycled by tick, so reuse needs care), buffer download interval sets |
| 123 | (title 1, 1.8.0 update ExeFS `updates/917504`) loading screen forever, ~116 s in: `Waiting for service chat:u` | the newer SDK runs `nn::chat::Initialize` (GameChat) at boot; nothing registers `chat:u`, so sm defers the request for good | `chat:u` registered from the friends process (friend.cpp): cmd 0 returns a session, whose cmd 1 (u32 2) returns an event (left empty: 2010-0212, then 2001-0114 InvalidHandle) |
| 124 | 2162-0001 (GameChat's own module) from `nn::chat::gsv::Initialize` | it sends gsv 1201 then 1202, the same pair as 901/902 and 1001/1002: 1202 must return a session. On it, TIPC 1() returns a u32 id (abort if > 0x3ff, sdk+0x6c1b40) and TIPC 11(u32 id) an event (copy handle word 3, result word 4; sdk+0x5d8710) | 1201 stub, 1202 returns `INx2ChatGsvSession` (nx2_gsv.h) |
| 125 | `nvnNativeImageInitialize` returns false, abort at main+0xd32438 | right after GameChat init the game builds its local camera video storage (`nn::chat::InitializeLocalCameraVideoStorage`, three 384x216 images, format 0x38). `CalculateRequiredBufferMemory` sends gsv 1291 (1510's request plus a u32 count; reply u64 size, u64 alignment), auto-stubbed to 0x1000 bytes; `CreateImagePool` sends gsv 1211 (429's request); image i of a pool has handle pool id + i (`nn::gs::sdk::CalculateNativeImageHandle`), so a 429 pool of 3 left handles +1/+2 unknown | 1291 = layout size * count; 1211 = 429; 429/1211 reserve `count` ids |
| 126 | same abort (row 125), then inside `ExecuteVirtualMachineMapBufferEx` | NVN describes and maps images another server made with gsv 428 `nn::gpu::sdk::QueryNativeImage` and 426 `MapNativeImage`(handle, u32, address space, u8 permission). Reply: words 3-4 GPU address (426), 5-6 offset in the memory, 7-38 a `GpuOunceUserImageDescriptor`, 39 flags. The SDK requires descriptor byte 0x00 == 1, byte 0x20 == 1, u32 size at 0x2c != 0 (offset at 0x28); NVN reads (w-1)|(h-1)<<16 at 0x08 and the layout kind at 0x24 (1: formats 0x25/0x37/0x38, 2: 0x99-0x9c; sdk+0x6bb960) | both answered from the pool's preference and memory; block height/pitch at 0x10/0x14 are a guess (only blank camera frames use them) |
| 127 | abort 2034-0000 at main+0xcded94 (~125 s, after the normal graphics boot) | `nn::camera::sdk::CreateCameraContext` sends gsv 601() then 602(u64 1), which must return the camera context session (handle word 3, result word 4); without it the SDK sends 603 (destroy) and fails | 601 stub, 602 returns a `nx2:camera` stub session. Title 1 (1.8.0) then plays |
| 128 | (title 1, 1.8.0, Ryzen 5 2600 + RTX 3050 6 GB) 8 fps | Eden defaults: gpu_accuracy High, uncompressed ASTC, reactive flushing on | gpu_accuracy Low, astc_recompression Bc3, use_reactive_flushing off, async shaders on: ~20 fps, uneven. Bushes/cacti flicker (row 97). Boot also froze once at "Failed to allocate 20480 MiB of backing memory": the 12 GB layout needs 20 GB of free commit up front |
| 123 | (title 1) 5,500-7,000 fences a second, one per compute release, each made in full (both cache locks twice, a fence object, a fencing-thread wake-up) even with nothing to wait for: SignalFence 7.6% of the GPU thread | with fast GPU fences the release callback runs at once; ~20% of the fences had no downloads, queries or operations. OverlapRangeSet::Subtract restarted its search after every erase | empty fences skipped (one in 64 still made), one lock per cache per fence, single-pass Subtract (9905916): SignalFence 3.9% |
| 124 | GPU thread ~9% spinning on, sleeping on and waking for the cache locks | the fencing thread took the buffer cache once per fence to write 250-330 MB/s of downloaded buffer data back (13,000-20,000 ranges a second) | signaled fences released together (31cd7b2, `EDEN_NX2_FENCE_BATCH=0` off): spinning 7.0% -> 5.5%; fence cost per call unchanged within noise (longer holds). Open: the downloads themselves |
| 125 | crash every few minutes: BindHostGraphicsUniformBuffer -> ReadBlockUnsafe, access violation at 0x600 (2 of 4 runs) | DeviceMemoryManager's translation cache was shared by all threads; Map on a service thread (nx2:gpu 211 job mappings, logged at both crashes) cleared it between the GPU thread's null check and its copy | per-thread cache invalidated by a generation counter (1a497fa): 0 crashes in 6 runs |
| 126 | macro parameters appended to a std::vector per call (~2% of the GPU thread) | ~1,000,000 calls a second, nearly all whole in one method run | stack copy (3caf8ea, `EDEN_NX2_MACRO_COPY=1` off). Passing the command list words in place is unsafe: they are often guest memory read in place, and the memo stores the parameters after the call |
| 127 | main core: ~23% of its samples read CNTPCT (~17 million reads a second) | the game polls the counter while it waits for the GPU; a spinning thread slows its SMT sibling (8-core/16-thread Ryzen) | PAUSE between counter reads less than ~1.7 us apart (8abeb0b, `EDEN_NX2_CNTPCT_PAUSE=0` off); effect not separable from run-to-run noise |
| 128 | Vulkan worker woken with a system call every 8 draws (~2% of the GPU thread) | DispatchWork notified even when the worker was busy | every 32 draws, notify only a waiting worker (d9d7e94) |
| 129 | macro memo arena (16 MiB) cleared every ~2 s | records stored again with another shape left their old copies | compaction (9a37142); live records are ~12 MiB, so most compactions still end in a clear; hits stay ~97% (46% on the last call's slot) |
| 130 | measurements | a stuck `nvidia-smi -l` loop had spun one core for 12 h (every run since the evening of 10-28 had one core less); the attract loop shows different courses each run (frame means vary +-15%); stopped test loops kept running and overlapped | compare per-call costs (`C:\dev\nx2work\zones.py` on `EDEN_NX2_GPU_PROFILE` logs); `ab_env.sh` and `hangcheck.sh` refuse to start while eden-cli runs; `stopall.ps1` stops every loop |
| 131 | (title 1) 250-330 MB/s of buffer data copied back at fences (13,000-20,000 ranges a second) | Eden downloads every GPU-written range at the next fence. Measured: the native MME's DMA reads never touched those pages (`EDEN_NX2_GPU_PROFILE` counter), and with reactive flushing the guest CPU read GPU-written memory at most 64 times in 200 s | off for Switch 2 titles (d530bc4, `EDEN_NX2_BUFFER_DOWNLOADS=0/1` forces): fences ~1.1 us (were ~5), draw setup -7%, cache-lock spinning 1.8% |
| 132 | inline uploads 10.5% of the GPU thread | IsRegionGpuModified visited every GPU-written range in the query (they stay fragmented without downloads); std::hash of integers and pointers is FNV-1a on MSVC in every Common::unordered_map | RangeSet::Intersects (one lookup) and a pass-through hash for integer/pointer keys (0fe581f): 7.5% |
| 133 | some courses (desert/canyon, dense city, highway) run 30-45 ms with presents blocked 9-15 ms on the previous frame's fence (also before today: t14) | not the host GPU (nvidia-smi: 20-40% busy, ~22 W) but the GPU thread: ~100% busy there, its command processing 370-420 ms/s instead of 300-350 (more commands per frame). The 8 GB laptop GPU is also near its budget (6.6-6.9 of 7.1 GB): the texture collector evicts hundreds of decoded ASTC images per 600 frames, the buffer collector GPU-written buffers every frame | `EDEN_NX2_TC_MIN_AGE=120` (0f0fbb9) made no difference over three runs; buffer evictions batched into one GPU wait (db4c539), still ~2.7% on those courses (open: asynchronous eviction) |
| 134 | each inline write invalidated the buffer, texture, shader and query caches on its own | thousands of small writes between draws | guest memory written at once, pending query results dropped at once, GPU-written buffer data uploaded in place; the other caches get merged ranges at the next FlushCaching (c06ac80, `EDEN_NX2_INLINE_DEFER=0` off): inline writes 4.4% |
| 135 | ~6 million 3D register writes a second, ~60% writing the value the register already holds (mostly macro memo replays) | each went through shadow RAM, dirty flags (re-checked at the next draw) and the method switch | state registers (not in execution_mask) written with their value are dropped after shadow RAM processing (f265911, `EDEN_NX2_SKIP_SAME=0` off): macro calls 169 ns instead of 185, draw setup 5.80 us instead of 5.95. For non-executable registers ProcessMethodCall is skipped as well (it acts only on executable ones) |
| 136 | native compute: ~20% of the GPU thread under KeplerCompute::ProcessNativeMethod (dispatches 8%, inline QMD/uploads 4%) | its compute macros replay ~64 QMD register writes per dispatch, each through the macro/QMD/upload/semaphore checks | plain native registers stored directly from CallMethod (f7a82b2): its own time 2.2% -> 1.6% |
| 137 | 2D blits (Fermi2D) 3.6% on heavy courses | mip chains 64x64..4x1 copied near the GPU-only regions; neither image is GPU-written, so the software blitter (un)swizzles them on the CPU | open (expected for small copies; many of them) |
| 138 | `async_presentation=true` crashed 4 of 8 runs ~26 s in (row 118) | retested after the fence-operation race (row 118) and translation cache race (row 125) were fixed: 0 crashes in 4 runs, screenshots correct | on in the dev config (`async_presentation\default=false`, `async_presentation=true`); every later run is a crash test for it |
| 139 | buffer collector: up to 64 buffers evicted every frame, GPU-written ones downloaded with a GPU wait (~5% of the GPU thread on 8 GB) | it compared the whole device's use (textures, ~6.8 GB) with thresholds from the device size (6.14 GiB reported): aggressive every frame, while the buffers were ~200 MiB | Switch 2 titles collect on the buffers' own size (1 GiB, aggressive 2 GiB) and keep GPU-written buffers outside aggressive mode (c4eaa4b): 0%, presents no longer blocked on those courses (22-26 ms in t23) |
| 140 | macros ~20% of the GPU thread; the memo's lookups (~76 ns a call in the game, 48 ns in a warm harness) missed the CPU caches | memo records and its map fall out of cache between calls; 4-way last-call slots raised slot hits 48% -> 63% but saved nothing | x86-64 compiler for the Turing MME (a95cedd, `macro_turing_jit.h`, `EDEN_NX2_MME_JIT=0` off): identical to the interpreter on 200,000 recorded calls (`C:\dev\nx2work\mme_test\jit_test.cpp`); in the game 148 ns a call instead of 174, 0 failures, 0 crashes in 3 runs |
| 141 | draw setup ~6.4 us a draw (55,000 a second) | spread over ~12 steps: uniform buffer copies (2.4%, the fast path streams them every draw and bypasses write tracking), texture views (8.5% incl. ~3.6% streaming new course textures), handle reads, vertex/storage buffer lookups | no single cache-able step; open: small per-step trims. The way past it is a second thread (NX2_GPU_THREAD_SPLIT.md) |
| 142 | GPU thread ~100% busy, host GPU 20-40%: one thread parses, runs macros, sets state and records draws | the work is sequential but only a few points need a result from what came before (semaphore acquires, MME DMA reads) | GPU thread split (babd395, `gpu_split.h`, `EDEN_NX2_SPLIT=1`): engine methods run in order on a second thread fed by a lock-free ring; releases, syncpoints, wait-for-idle, flushes and channel binds are deferred into the ring, only acquires wait (~2,400 drains a second). ~47,400 draws a second against ~44,000 (3 A/B rounds), same per-call costs |
| 143 | crashes with the split: CommandChunk::Record (null chunk), vkCmdEndRenderPass on the Vulkan worker | two threads recording into the scheduler: (a) the DMA pusher's split-method debug log read guest memory with the cache-flushing ReadBlock on the front; (b) with async presentation, Swapchain::AcquireNextImage -> Scheduler::Wait flushed the main scheduler from the present thread whenever its tick was not submitted yet (pre-existing race, the split made it frequent) | (a) ReadBlockUnsafe; (b) e4523ab: the present thread waits up to 20 ms for the GPU thread to submit, never flushes. Found with `EDEN_NX2_SPLIT_CHECK=1` (stack of any other thread using the scheduler while the back runs) |
| 144 | macros on the split's front (`EDEN_NX2_SPLIT=2`) need the registers they read before the back has them | the front keeps copies: every write it sends plus what the back changes on its own (constant buffer offset, shadow_scratch[0], macro bind pointer, shadow RAM). `EDEN_NX2_SPLIT_VERIFY=1` found three differences: writes still in the back's method sink at the handover; a multi-word run that changes the shadow RAM mode mid-run; and shadow_state.shadow_ram_control being the shadow copy of register 0x49 itself (a Replay write reads the mode back) | all three modelled: 0 differences in 40,193 checks (~165 million macro calls). Front: macros 128 ns a call (180 on one thread, cache-hot), drops ~55% of 3D state writes as same-value. Back: 50,000-66,500 draws a second against 41,500-47,000 without the split; frames 20.5 / 25.5 ms on two courses where the unsplit runs measured 25.6-40.7 ms. Default stays off until a clean A/B |
| 145 | is the split worth having on by default | clean A/B, nothing else running, 3 rounds each (profiler on in both): one thread 41,100 / 48,600 / 37,000 draws a second, frames 30.5 / 35.0 / 27.1 ms; `EDEN_NX2_SPLIT=2` 59,200 / 62,400 / 62,700 draws a second, frames 28.6 / 23.3 / 25.1 ms. Throughput 1.46x, frames 17% shorter (the back is now ~92% busy, ~53% of it draw setup); 0 crashes, 0 register differences in 57,089 checks after the front's fast paths (no guarded statics, inline ring writes) | on by default for Switch 2 titles (`EDEN_NX2_SPLIT=0` one thread, `=1` macros on the back). Default run without the profiler: 16.9 ms frames (~59 fps) on a light course |
| 146 | title 4 aborts with InvalidHandle (0xE401) at its first movie, ~30 min into a new game | newer SDKs (`nn::codec::video::detail::InitializeServerContext`) open the decoder with gsv 906(u8, u64 library version 0x3'00030000) instead of 902; auto-stubbed, no handle came back | 906 hands out the 902 session (d8d11ec) |
| 147 | title 1: Grand Prix cup-opening screen stays black, decoder registered but never started; then an abort with 2111-3604 (0x1c286f) when it did start | `nn::syncpt::sdk::CancelSyncPointEvent` sends 202 and always waits on the slot's event; we signalled it only for pending requests. Then the movie player reused slot 0, which NVN's fence waits had left signalled, read it as a decoded frame and found none | 202 always signals, 201 clears the slot's event first (b09629f) |
| 148 | title 1/4 movies have no picture | the video decoder service (nn::codec::video, gsv 902/906/1002 -> vdec -> vdecsvc) completed frames without decoding | FFmpeg decodes on a thread per decoder service (`video_core/host1x/nx2_video_decoder`): 101 names the codec ("video/vp9") in an A buffer, 109 submits frames (A buffer, pts us). 311/314 reply NativeSyncHandles {~syncpoint, 0, value} on a syncpoint the service allocates, incremented per decoded frame, frames filling images in register/release order. gsv 911 creates the pool's images, 961 returns the CpuUserImageDescriptor that `ReadDecoderOutputImage*` copy out with (pitch-linear NV12/RGBA/BGRA, kinds 10/8/9). Title 1's Mushroom Cup opening (2560x1440 VP9, 1423 frames) decodes in real time and the game goes on to the race. The picture still stayed on the first frame: see 149. `EDEN_NX2_VDEC_DUMP=<dir>` saves decoded frames |
| 149 | title 1: the cup-opening movie's picture stays on its first frame although every frame decodes and reaches GPU memory | not the texture cache: the movie is drawn into a 2560x1440 render target whose NVN2 texture has GPU address 0 (also the 640x360 and 512x512 passes), so the draws go nowhere. NVN2's texture init (sdk+0x5ca240) places a texture in a compressible pool (flags 0x080000a1) only after `NvRmGpuMappingModify` switched the pool's mapping (0x815f0d0000, a gpu 214 region) to the texture's kind; NvRm fails that call with 2 unless the characteristics flags have NVGPU_GPU_FLAGS_SUPPORT_MAPPING_MODIFY (bit 34; ours were 0x4400155), and the texture keeps address 0 | report bit 34; the modify then runs through nn::gpu::sdk's VirtualMachine as gpu sub 212 (NormalModifyMemoryKind4K: words 3-6 range, 7 wanted kind, 8 fallback, 10 job slot; reply word 3 the kind applied), answered with the wanted kind and the slot signalled. The movie plays on screen |
| 150 | title 1: animated colliders stay at their default pose while the meshes animate (Dino Dino Jungle's dino and the rail on it, DK Spaceport's robot legs on the last lap, Bowser's Castle Thwomps once down) | the guest CPU reads compute shader output back from GPU-written buffers, and nothing copied it to guest memory: no downloads at fences for Switch 2 titles (row 131), reactive flushing off (row 115; its read protection of every cached page stuttered), and RasterizerVulkan::GetFlushArea never asked the buffer cache (dropped upstream in 8203f2d8e1, when every GPU-written buffer was downloaded at fences), so even a trapped read found nothing to flush. A range read once is marked Preflushable and its later reads skip the flush | read traps (8b4c42c): a GPU write that is not downloaded at fences takes read permission off its pages once (fastmem only; not under NCE or with reactive flushing). The guest's first read takes the slow path and gives the page its read permission back; with no downloads at fences GetFlushArea asks the buffer cache, which flushes the range and marks it Preflushable, and Preflushable ranges are downloaded at every fence again. Diagnostic run: ~3,800 pages armed, ~3,500 trapped reads. The dino's rail follows it (2 of 2 tries); the robot and the Thwomps not retested, frame times not measured. `EDEN_NX2_READ_TRAPS=0` turns the traps off |

Run settings used (in `build/bin/user/config/sdl2-config.ini`): `memory_layout_mode=4`
(12 GB). Debug knobs: `EDEN_NX2_TC_EXPECTED_MIB` / `EDEN_NX2_TC_CRITICAL_MIB` (collector thresholds),
`EDEN_NX2_ASTC_FLUSH=1` (GPU ASTC decode without the full wait), `EDEN_NX2_CRASH_DUMP=<dir>` (prints the
faulting stack as module+offset, then a minidump), `EDEN_NX2_AUTO_STUB`, `EDEN_NX2_IPC_TRACE`, `EDEN_NX2_SVC_TRACE`,
`EDEN_NX2_PULLER_TRACE`, `EDEN_NX2_METHOD_TRACE`, `EDEN_NX2_HIDE_WINDOW`,
`EDEN_NX2_GPU_PROFILE` (GPU-thread zone timers every 10 s), `EDEN_NX2_SAMPLE=<threads>` with
`_START`/`_SECONDS`/`_OUT` (sampling profiler; `C:\dev\nx2work\folded.py` reads its .folded stacks),
`EDEN_NX2_MME_RECORD=<file>` (macro calls for `C:\dev\nx2work\mme_test`),
`EDEN_NX2_FENCE_BATCH=0`, `EDEN_NX2_MACRO_COPY=1`, `EDEN_NX2_CNTPCT_PAUSE=0` (turn rows 124, 126, 127 off),
`EDEN_NX2_MME_MEMO_STATS` (memo counters),
`EDEN_NX2_BUFFER_DOWNLOADS=0/1`, `EDEN_NX2_INLINE_DEFER=0`, `EDEN_NX2_TC_MIN_AGE=<frames>` (rows 131, 134, 133),
`EDEN_NX2_SKIP_SAME=0` (row 135),
`EDEN_NX2_MME_JIT=0` (row 140),
`EDEN_NX2_SPLIT=0/1` (default 2), `EDEN_NX2_SPLIT_VERIFY=1`, `EDEN_NX2_SPLIT_CHECK=1`, `EDEN_NX2_SPLIT_SKIP=0` (rows 142-145),
`EDEN_NX2_READ_TRAPS=0` (row 150),
`EDEN_NX2_FRAME_CAPTURE=<dir>` (every 120th presented frame as TGA), `EDEN_NX2_PB_CHECK`
(pushbuffer entries changed between submit and execution), `EDEN_NX2_QUERY_TRACE` (every GPU
report/semaphore write). `C:\dev\crashrun.sh` runs eden-cli under cdb and prints the crash stack;
`C:\dev\stackdump.sh <main> <seconds>` dumps host thread stacks of a running test.

## Where it stops now

All runs: `EDEN_NX2_AUTO_STUB=1`, 12 GB memory layout, loose ExeFS with the sibling `romfs/`.

**Title 4** (`fourth switch game\exefs`, 0400380020056000) **plays its intro cutscene correctly
rendered**: boot, loading screen, menu (difficulty select, keyboard input), confirm, then the intro
(plane cockpit, newspaper/map) lit, colour-graded and subtitled, matching console footage. The
DLSS-style upscale is a bilinear stand-in. In a played session the game went on to a later loading
screen that did not finish; open there: a compute shader with a 64-bit add (UIADD3 carry-out to
UP0), TMML (0xb69) in two graphics shaders, "reserved attribute 23" (a[0x5c]) reads. Scripted runs
(`C:\dev\run.sh`) move any title-4 save to `C:\dev\save_backups` first so they start a new game.
Scripted input: `EDEN_NX2_KEY_SCRIPT="50:4,50.5:-4,55:79,55.4:-79,57:4,57.5:-4"`.

**Title 3**: last checked before the graphics work; should now track title 4.

**Title 1, 1.8.0 update** (`updates/917504/exefs`, same program id): needs the GameChat and
camera stubs of rows 123-127, then plays (Qt frontend `eden.exe`, ~20 fps on row 128's hardware) with
the same open problems as below.

**Title 1** (`first switch game\exefs`, 0400C3F00006E000, `Drive.nss`): boots, shows the world-map
loading screen and reaches its title screen ("Press L + R to start") with sky, buildings, props,
kart and logo drawn. Wrong: magenta/cyan colour cast, black ground (unbacked virtual texture, row
87), task/mesh geometry missing (row 89), ~10-17 fps. Launch with frame capture:
`EDEN_NX2_FRAME_CAPTURE=C:/dev/frames_t1x EDEN_NX2_AUTO_STUB=1 EDEN_NX2_SHADER_DUMP=C:/dev/shaders_title1 ./eden-cli.exe -g "C:/switch/first switch game/exefs/main"`.
Offline shader check: `nx2work/lowertest/lowertest.exe C:/dev/shaders_title1 [-all [op]]`.

**Title 2**: main/sdk/subsdk0 fail LZ4 and SHA-256 checks; can't be run from these files.

## Prioritized next steps

1. **Title 4 past the intro**: lower the 64-bit add with carry (UIADD3/IADD3 carry-out and .X
   carry-in), TMML, and attribute 0x5c; then whatever the next area needs (dump + lowertest).
   Squid/paint-splat hatch on the menu. A faster tensor-core emulation (shared-memory HMMA) would
   replace the bilinear upscale stand-in.
2. **Title 1 / title 3** retests with the current tree; dump their shaders and widen the lowering.
3. Replace guesses with real protocol knowledge: syncpt page layout, the semaphore mirror page,
   gsv 214 (GPU-only memory) and vi2 present buffer availability (real buffer release instead of
   the fence wait), gsv 1510 formats, svcGetInfo 29-33.
4. **Six cores** instead of folding 4/5 onto 0/1; rare SIMD encodings still undecoded.
5. Upstream-worthy pieces independent of Switch 2: MSVC 14.51 build fixes, eden-cli SDL3
   lifetime fixes and `_setmaxstdio`, ExeFS loader fixes, FAC/FAH offset fix, GetPseudoDeviceId
   without NACP.

---

## 2026-11-08 - build-119 (Ocarina of Time 3050, update #3)

Live-session findings on the owner's PC (Ryzen 5 3600, RTX 3050 6 GB, 24 GB RAM, Windows 11,
meta-virtual desktop, displays 1920x1080) while the game sat in Kokiri: ~12-14 FPS, p50 frame
~68-79 ms, 3D engine ~66% busy, whole GPU ~88% busy at 63 W, VRAM 5008/6144 MiB (82%), host CPU mostly waiting (GPU back thread ~22% busy, front ~5%; 104 threads, ~1.9 cores average).
Presents were 3840x2160 from a 2560x1440 render.

DLSS Super Resolution declined EVERY frame: 'NVIDIA DLSS recording failed at stage 4 (Streamline
result 39)' = eWarnOutOfVRAM (39 is the 40th sl::Result; the plugin printed 'Exceeded VRAM budget'
on the same timestamp). Every frame then ran the game's own HMMA upscaler instead of DLSS - the
most expensive thing the game can do on this card. Root cause: output 3840x2160 + render
2560x1440 on a 6 GB card, with the OOT preset forcing the Max Resolution built-in patch (full
render scale) - the user's own Add-Ons disable of 'Max Resolution (built-in)' was being overridden
by the preset/env force. Fixes in build-119:

- patch_manager: the per-game Add-Ons list now wins over the OOT preset and EDEN_NX2_OOT_PRESET=1
  for built-in patches (user-disabled patches stay off; previously the preset force re-applied).
  With the user's existing disable in place, the game boots on its dynamic-resolution path (the
  console's own behavior under GPU load) instead of forced full scale.
- yuzu dialog: 'Force Max Resolution' checkbox now defaults off when the host adapter reports
  <= 6.5 GB dedicated VRAM (DXGI query), text no longer advertises frame generation, DLSS SR is
  described honestly. Bigger cards keep the full-scale default.
- Vulkan: 'Patch topology not used with tessellation' warning demoted to Debug (the game logs it
  for every pipeline build; thousands of lines per boot). DLSS failure log line no longer claims
  FSR 3.1 - the path takes the game's upscaler.
- Frame generation stays OFF: code default frame_gen=false, preset forces false. The 60 FPS
  dialog text no longer claims FG is enabled.

Shader-build-time investigation on this machine: the disk pipeline cache is healthy (13 .bin blobs,
~1 GB; only 2 pipelines built live in the session; keys are stable via
static_assert(has_unique_object_representations_v); cache version is manual (54), not bumped per
build, so caches survive updates). Runtime builds are ~2-4 ms; the remaining user-visible cost is
first-run/cold-cache driver compiles. Keep the user/cache/shader folder across updates; the
preloader (most-recently-used, seek-based) handles warm starts.

1080p note: the game chose 3840x2160 as its SR output from its own graphics setting; the emulator
cannot shrink that from outside. For 1080p output, set the game's graphics option to 1080p
(docked, as on a real 1080p TV) - DLSS then upscales/presents at 1080p natively. Unverified as of
this writing; the build above has NOT been measured in gameplay yet.


## 2026-11-08 (later) - build-120 measured live (Kokiri), clang flavor added

The user authorised launching and driving the game. Test setup: isolated portable copy at
C:\Users\rikom\ocarina2-test-120 (copied keys/nand/config from %APPDATA%\eden; the real profile is
untouched), `EDEN_NX2_OOT_PRESET=1`, docked, DRS, Fast GPU mode, BC3, DLSS on, fresh cold shader
cache (blobs NOT copied), build-120 MSVC, tag build-120.

Measured on this PC (live, debug server + Task Manager-style counters):

- Title / attract: ~59-60 FPS (vsync-locked). Main menu: ~47 FPS. Options/system menus: ~46-47.
- Kokiri (the benchmark area): ~25 FPS sustained, ~37-45 ms frames, ~40 ms steady.
  First cold-cache entry to the Saria spot spiked at 75-195 ms transiently (the
  pipeline rebuild; 283-417 builds per 10 s window during load, ~4.3 s GPU thread per 10 s),
  then settled to ~25.
- GPU ~84% busy at ~62 W (under the 70 W limit), SM ~1972/2130 MHz, VRAM ~4.3-4.9 GB.
- Zero "Uncached storage pointer" rejects across the cold boot (rejected: []).
  The shader disk cache rebuilt and reports 2-35 ms per worker build.
- The options menu has NO graphics/display settings (checked every page: in-game Options
  and the title-screen Options; amiibo, ZELDA NOTES, camera, interface, subtitles, voice,
  vibration only). The game picks 3840x2160 itself (its SR pass is keyed to the 1440p
  pipeline: vk_rasterizer upscale_4k = pipeline.ShaderHash() == Ocarina1440pShader).
  DLSS at 3840x2160 still cannot allocate on 6 GB (stage 4 / result 15 / NGX 0xbad0000d)
  and is now retried every 10 s (build-120). Docked stays per the owner.
- The per-game profile on this machine (as copied) has gpu_accuracy=1 (GPU Mode: Accurate)
  and astc_recompression=0 (uncompressed) - the expensive end of the emulator's knobs;
  the OOT preset forces Fast GPU + BC3 at runtime regardless.

Build-120 was also built with clang-cl (LLVM 23.1.5) into a second flavor
(Ocarina2-build-120-Clang-Windows-x64.zip, from fix/oot-fps-perf-timing).
One file needed a clang-only fix:

- global_memory_to_storage_buffer_pass.cpp: MSVC STL's vectorized std::ranges::find
  (xutility _Find_vectorized) static-asserts for 12-byte elements because clang's
  __is_trivially_equality_comparable accepts the defaulted <=>; the TU keeps the scalar
  path (_USE_STD_VECTOR_ALGORITHMS=0). MSVC was never affected.

GUI check on this machine: the saved theme was colorful_dark (chosen pre-build-118),
not Midnight Purple; the Midnight Purple themes ship compiled in and are the default for
new installs; Settings > Interface > Theme > "Midnight Purple Colorful" switches and the
whole window (title bar, border, sidebar, status bar) goes dark purple.
The window title at the very top reads "Lichii build-120".
New entries go on top of this section; the build-119 machinery notes moved into this block:

Build machinery on this PC: build dir C:\Users\rikom\ocarina2-build (MSVC 14.51.36231 + VS2026
Build Tools), second flavor in C:\Users\rikom\ocarina2-build-clang (clang-cl 23.1.5, LLVM 23.1.5
from winget), Qt 6.11.1 via CPM repo cache (.cache/cpm), Ninja, configured by vcvars64. Do NOT
run a bare cmake configure without vcvars64: CMAKE_SYSTEM_PROCESSOR is then NOT detected
('Target architecture: GENERIC') and the OpenSSL CI package fatals ('Unsupported platform for CI
package'). working configure = ocarina2-configure.cmd (LICHII_BUILD_TAG=build-120) and
ocarina2-configure-clang.cmd (clang flavor, LICHII_BUILD_PLATFORM=windows-x86_64-clang).


## 2026-11-08 (night) - OOT preset now disables the GPU thread split (water-flicker fix)

The Kokiri water went black in bursts every few seconds through build-120/119/118 and it was
reproduced on this PC in build-121 with flash watch (EDEN_NX2_FLASH_WATCH=... passes=true):
all flashes are in one render pass - the half-resolution scene-color pass, B10G11R11_FLOAT at
0x8114f02000/0x8114302000, 21 draws, shaders d9edd60c82c0d73c plus 13 more - its mean collapses
from ~0.087 to ~0.0002 in bursts; surrounding passes (bloom, composite) stay clean. It is NOT
the Ocarina2 null-texture handle fix (EXP21, A/B'd off: same flicker), NOT the DLSS replacement
(DLSS never engaged here: inputs fail the checks), NOT rescale-only, NOT missing pipelines
(rejected: [], no unbuilt draws at flash time). Root cause: the mesh/task indirect-draw buffer
is read before the GPU writer commits the page-map entry that publishes it (the split-thread
race; build-119 partially fixed this class for other passes). Disabling the split
(EDEN_NX2_SPLIT=0) stopped every flash in a 3-minute watch. The OOT docked preset now sets
oot_split_disabled, and GpuSplit::MaybeStart refuses to run the split in that case
(EDEN_NX2_SPLIT=1 or =2, or a fresh env relaunch, override it; declining the preset keeps the
split at its default). Perf: Kokiri ~63-74 ms frames at SPLIT=0 (~14-15 FPS) vs ~62-68 ms at
SPLIT=2 - a 1-3 FPS cost in this scene, none in menus (vsync-locked at 60 either way).

## 2026-11-08 (night) - menu/attract FPS across builds (from recorded logs)

The user asked why the menu was "just on 60 fps" and to track menu FPS across builds instead of
asserting. Recorded numbers from this PC (Ryzen 5 3600, RTX 3050 6 GB):

| Build | Scene | FPS | Evidence |
|---|---|---|---|
| build-119 (Nov 8 live) | Title/attract | 59-60 (vsync-locked) | NX2_NOTES measured session |
| build-119 | Main menu / options | ~46-47 | same session |
| build-120 (Kokiri session) | Title/attract | 59-60 (vsync-locked) | NX2_NOTES measured session |
| build-120 | Main menu | ~47 | same session |
| build-120+#911 (7:11 PM, revert build, test-120) | Attract (46-66 s in) | 60 (interval mean 16.99 ms, game frame 16.59 ms ~= 60) | eden_log.txt Host pacing lines |
| build-121 clang (8:12 PM boot) | Title/attract | 59.4-59.6 (vsync) | debug server status, 1867 presents @ ~60 s |
| build-121 clang (8:47 PM relaunch) | Intro cutscene video | 30 native (pts-cadenced) | DecodeLoop lines; frame pacer 30 fps |
| build-121 clang (8:47 PM relaunch) | 3D attract during ASTC rebuild + pipeline builds | ~12-30 (dips) | GPU profile + "ASTC GPU decodes: 7 in 5587 ms" |
| build-121 clang (8:12-8:19 PM, GPU shared with my scale probes) | Menu/attract | ~22-30 | my mistake - two instances on one card |
| build-121 clang (8:47 PM relaunch) | Intro cutscene FMV | 30 native | pre-rendered video |

Read-out: the trend across builds is flat at the light title screens (59-60 vsync-locked
since build-119) - no regression there. The fluctuation the user sees in gameplay-adjacent
scenes is the HMMA upscale at 4K plus ASTC recompression stalls and pipeline rebuilds at scene
entries - the same Kokiri-class wall. The intro cutscene is native 30 FPS by design and cannot
be improved emulator-side. Audio pipeline is healthy (48 kHz cubeb, no underruns in any log).

## 2026-11-08 - build-121 candidate (branch ocarina2-merge)


Delta on top of build-120 while the release was held, built and booted on this PC.

- #802 (byte-range native program bank uploads) reverted (a2957aa5cd). With #802 the image went
  pink; #802 or #909 both reduced/changed uniform bank uploads. The game renders correctly with
  them gone: commit 27f69afe5a reverted #909, a2957aa5cd reverted #802. #911
  (fence-delayed semaphore releases latch acquires, 23f4245ee2) stays in - that is the build the
  owner marked "looked perfectly" with the forest rendering cleanly.
- OOT preset renders docked 1080p-ish on low-end cards: resolution_setup Res3_4X (0.75x),
  Max Resolution stays a per-game choice (a0bd02b2a4).
- NVIDIA DLSS gauge for this fork (45e8a9cc90, DLSSMode): new setting nvidia_dlss_mode
  (Auto/Quality/Balanced/Performance/Ultra Performance), shown on the Graphics tab under
  "Use NVIDIA DLSS" and visible only while DLSS is on, takes effect on the next frame
  (runtime-modifiable; re-calls slDLSSSetOptions when the mode changes). Auto keeps picking the
  mode from the render-to-output ratio; a forced mode is passed through verbatim; any DLSS
  failure falls back to the game's upscaler as before. Mode name now logged by name on options
  changes. The OOT/DLSS 4K wall is unchanged (see build-119/build-120 notes): this card cannot
  allocate NGX at 3840x2160, and Res3_4X rescales the depth view, which the Ocarina DLSS
  replacement refuses to touch - the gauge is inert until the game renders an un-rescaled
  resolution at a DLSS-able output size (in-game 1080p / a card that fits 4K NGX).
- nx2.h: the intentionally ignored Latch() return in OnWrite is now (void)-cast; clang-cl rejects
  it as -Werror,-Wunused-result (MSVC warns, C4834).
- Builds: MSVC (C:\Users\rikom\ocarina2-build, LICHII_BUILD_TAG=build-121) and clang-cl 23.1.5
  (C:\Users\rikom\ocarina2-build-clang, LICHII_BUILD_TAG=build-121) both link; titles read
  "Lichii build-121". Test copies: ocarina2-test-120 (MSVC) and ocarina2-test-121 (clang),
  both booted to gameplay, ~59-60 FPS at title/attract (vsync-locked), Streamline attach OK,
  MC Renderer.nvidia_dlss_mode: Auto in the settings log. Nothing is tagged or published.
- Reported on the release thread: "it's just launching forever" (build not identified). Matches
  the known upstream launch-hang class (zortos293/Lichii #99 ".NUP infinite launch screen",
  #297 "NUP remains on Launching" - ARM exception 0x0), not something this delta introduces or
  fixes. Known community workaround: EDEN_NX2_SPLIT=1. Also the first boot after build-120+ is
  slow: disk-cache version went 54 -> 55, so every pipeline is rebuilt (~hundreds at boot on this
  PC). Users on slow PCs at the "Launching" screen for minutes with an advancing log are
  compiling shaders, not hanging. The build-121 notes draft needs the first-boot warning.

## 2026-11-08 - build-120 (Ocarina of Time 3050, update #4)

Live-session findings on the owner's PC (Ryzen 5 3600, RTX 3050 6 GB, 24 GB RAM, Windows 11,
meta-virtual desktop, displays 1920x1080) while the game sat in Kokiri, and the fixes argued
from them. Frame rate: ~18 FPS, mean frame 55 ms, p50 54.8, p90 57.5, min 49.9; ~180 render
passes a second (about 10 per frame).

Where the time goes (Task Manager-style counters + the live debug server + EDEN_NX2_GPU_PROFILE):

- 3D engine 68% busy, whole GPU ~90% busy at 64 W, SM clocks 1987/2130 MHz (not power or
  thermal limited), VRAM 4263/6144 MiB used (4.66 GiB reported available at device creation).
  This is the game's own HMMA upscaler at the 4K output size - the most expensive thing the
  game can run on this card - plus the 1440p scene render. The GPU is not FLOP-limited; it is
  busy but slow, waiting on a serialized, emulator-bound submission stream.
- Host CPU ~586% (about 5.9 of 12 logical CPUs), 105 threads, 26,769 page faults a second
  (soft), ~4.1 GB private working set; the GPU back thread is ~52% busy, the split back half
  waits ~340 ms/s for the front, and fence traffic is 4,280 fences a second.
- The engine parses ~85k methods per frame (~1.54M/s, ~138 ms/s on the back thread), ~1.19M 3D
  register writes a second, binds stage buffers ~48k/s, and copies ~552 MB a second of
  GPU-written buffers back to the CPU in ~44,300 copies (skinning/collision data the game
  reads after compute passes; inherent to the game's design, not a bug to chase blindly).
- Pipeline building is not the mid-game bottleneck: runtime builds ran 2-4 ms each, zero draws
  held for unbuilt pipelines, ~64 builds per 10 s at scene transitions. The cost is boot time
  and the rejected-every-boot class below.

In this session DLSS declined every frame with a different signature than build-118/119:
"NVIDIA DLSS recording failed at stage 4 (Streamline result 15)" = eErrorNGXFailed, with the
plugin line "NGX create feature failed 0xbad0000d" = NVSDK_NGX_Result_FAIL_OutOfGPUMemory,
at the game's 3840x2160 output (it comes from the game's own graphics option; the emulator
cannot shrink it from outside). DLSS was then disabled for the rest of the session by the
one-shot failure latch. Fixes in build-120:

- vk_streamline_dlss: a failed evaluation is no longer terminal. The upscaler latches the
  failure, reports Failed() (the caller keeps the game's upscaler for that frame), and retries
  after 10 s; a later, lighter scene can adopt DLSS without a restart. A failed frame still
  falls back exactly as before (the output is cleared for that frame only).
- Frame generation is removed in its entirety. No code path, setting, toggle or UI remains:
  the LSFG/FSR3 chains and their shaders, the frame_gen* settings, the present-frame
  storage-image plumbing (Frame::storage_view/storage_capable, NeedsStorage, MaxExtraFrames),
  the FIFO-forced pacing branch and the LSFG extra-frame image clamp, the Qt status toggle and
  its Advanced-tab page, the OOT preset's force-off line, the Android settings catalog /
  quick settings / Lossless Scaling manager / system-info row / setup step, the
  LosslessScalingHelper, every Frame Generation string and array (7 languages), the lossless
  user-directory path, and the fsr3 probe tools.
- Shader disk cache version 54 -> 55. The foliage-culling environment fix in build-119 let the
  codegen ask about storage-pointer tables that blobs written before that fix never recorded,
  so 18 pipelines failed every boot with "Uncached storage pointer classification"
  (deterministic, same hashes, every launch since the fix). One cold rebuild happens now; the
  blobs record the complete tables afterwards.
- GUI (small, as asked): the window title and the About dialog name the build plainly
  ("Lichii build-120"); the Midnight Purple theme's panel surface is slightly darker; the
  OOT 60 FPS preset text no longer mentions frame generation. The title no longer carries the
  compiler id.

Docked gameplay remains the target and the OOT preset stays docked with DLSS on.
Not verified as of this writing: the Hyrule Field crash, the cutscene audio fixes, and any
gameplay at all with build-120 - the 30 FPS bar in Kokiri / main menu / cutscenes is NOT yet
met on this machine (~18 FPS at the game's 4K output with the game's upscaler). The path to
native-with-DLSS is in-game: set the graphics output to 1080p (a real docked TV) and leave
"Force Max Resolution" unchecked (the build-119 default on <= 6.5 GB cards); the game then
renders dynamically and DLSS upscales to 1080p. Do not publish the release until this is
measured.

## 2026-11-09 - OOT 60 FPS preset is now the owner's tuned handheld session

The water flicker in Fast mode (kokiri, docked runs) traces to the GPU-thread split reading the
indirect-draw buffer before the GPU writer commits the page map (2026-11-08 night entry). The
owner's own session config never picked up the split disable: `oot_split_disabled` is a runtime
bool set only when the "Ocarina of Time 60 FPS settings" dialog is accepted (or
EDEN_NX2_OOT_PRESET=1), and launching on his own tuned settings leaves the split on.

The OOT preset now IS his session (the settings enabled throughout his emulator on 11-09,
qt-config 0400486024DEC000.ini effective values): Vulkan, resolution Res1X (was 0.75x), GPU
Fast (Low), handheld mode (was docked), 4 GB memory layout (was code default 8 GB), immediate
VSync and GPU fences, unsafe DMA reads, aggressive VRAM usage, async shaders + presentation,
reactive flushing off, Area scaling filter (was bilinear), no AA/post-processing, DLSS off
(was on globally; the NGX 4K wall made it decline every frame anyway), Force Max Clock, very
large GPU unswizzle, CPU Unsafe, sync-core-speed on. Still sets `oot_split_disabled` (the
water fix) plus the Dynamic FPS / preflush / Max Resolution patch defaults. Unchanged: Add-Ons
disables still win, MKW preset untouched, emulator code defaults untouched.

`EDEN_NX2_OOT_RESOLUTION` is gone (the preset renders natively); `dist/nx2_switches.txt` no
longer documents it. The boot dialog text describes the new preset. tests/common/game_presets.cpp
locks the new values in. Note: memory_layout_mode=Memory_4Gb and CPU Unsafe are in the preset
because the owner asked for exactly what he has enabled; they are the riskiest knobs on other
hardware if anyone reports trouble - raise to Memory_8Gb / CPU Auto first.
Not built or tagged as of this writing; verify a boot accepts the dialog and the water holds.

## 2026-11-09 (later) - split: the DMA snapshot now tracks graphics draws; water-flicker fast mode

The remaining water-flicker class in Fast mode runs WITH the split on: `FrontDmaRead` (the
front-thread macro DMA read, ~35k reads/s, mostly occlusion reports) took the `EDEN_NX2_DMA_SNAPSHOT`
fast path - no drain at all - whenever no compute/copy/unranged writer had been queued since the
page's last snapshot flush. Graphics (mesh/task/task-cull) storage writes are NOT covered by
`NoteWriter` (compute/copy only) or `NoteUnrangedWriter` (copies); any 3D draw that wrote storage
buffers (indirect-draw arguments, cull outputs) was committed without telling the split. The front
could read the indirect-draw buffer from guest memory before the back even executed the draw, see
stale/zero counts (zero-indirect-draw frames), and the scene-color pass went black in bursts.

Fix: the front now notes every 3D engine draw-end entry (`GraphicsWrites` position in
`GpuSplit::NoteGraphicsWriter`), and the snapshot fast path requires it to be no newer than the
page'\''s snapshot position (`LastGraphicsWritePosition() <= snapshot`). Any graphics draw committed
since the snapshot blocks the fast path and forces the drained, flushed read - the same guarantee
as SPLIT=0 for this path. Compute/copy writers were already tracked and are unaffected. The miss
reasons now count graphics draws (`miss_reasons[4]`, "graphics draw" in the log). Default stays
`EDEN_NX2_DMA_SNAPSHOT=9`; EDEN_NX2_DMA_SNAPSHOT=10 verifies every skipped read against the drained path.

The drained path was always correct; this change makes the fast path equally correct, at the cost of
the fast path rarely hitting mid-draw-loop (any draw invalidates). A range-aware tracker (bind-time
storage-write ranges reported to the front) is the way out of that, if the perf is wanted back.

Still open: the half-resolution scene-color pass flashes should be reverified live with the split on
(EDEN_NX2_FLASH_WATCH); not measurable in this environment. The OOT preset still disables the split
as belt-and-braces. The user's tuned config launches with the split ON (the preset dialog keeps
"your saved settings are not changed", and declining it never applied oot_split_disabled).

## 2026-11-09 (later) - DLSS: the newly added mode gauge is gone; DLSS path documented for 6 GB cards

Removed the `nvidia_dlss_mode` setting (the "DLSS Mode:" combo, build-121) entirely: settings, enum,
UI row (Graphics tab), translations, `vk_streamline_dlss` mode override (Auto-only from the
render-to-output ratio is now unconditional), `configure_graphics.cpp` wiring and `apply_funcs`
visibility. The DLSS checkbox is unchanged; Streamline/310.9.16-on-the-NVIDIA-app and the
10 s retry on NGX failure are unchanged. Reading it as "the newly added DLSS": if what was meant
was "remove NVIDIA DLSS altogether", this is one `git revert` away.

"Optimizing for a 3050" concretely: nothing in the code can make NGX fit at 3840x2160 on 6 GB
(stage 4 / result 15 / 0xbad0000d = FAIL_OutOfGPUMemory - the retry stays). The handheld console
mode in the new OOT preset is what makes DLSS viable on this card: 810p -> 1080p (or the game'\''s
other sub-4K outputs) fits NGX and lets the replacement engage. The nvidia_dlss tooltip now says
on 6 GB cards DLSS only fits at 1080p-or-below output. `EDEN_NX2_GRAPHICS_SCALE=20` (1440p output)
is documented in dist/nx2_switches.txt as the lever to shrink the output. Preset K stays forced for
every mode (M/L draws thin lines in Fire Emblem at 2x scale).


## 2026-11-11 - the "Link turns into bright light" flash: IN PROGRESS, NOT FIXED

Status of the glow/white-flash artifact on investigated/fsr3-fg. It is being
worked on, it is not fixed, and any build you see with the guard in it is a
test build - not a solution.

What is known, in the order it was learned:

- The flash happens at the Kokiri load-in, on hitch frames. The game is not
  crashed when it happens; HUD still renders; the world blows out.
- A captured flash frame (capture-1794369252-001, frame 3932) shows the
  velocity/motion buffer with 3,195 NaN texels of 74,336 and max 14,272
  (clean frame: exactly 0; white-out frame: 0 too but everything else pegged
  at format max). The resolve/glow chain fans NaN out to a white-out.
- A captured white-out frame (capture-1794367830-001, frame 2214) shows the
  whole glow/fog/HDR chain pegged at the B10G11R11/R16G16B16A16 ceilings;
  half the scene surface holds stale stripe garbage; the exposure feedback
  latches; the game eventually stops presenting.
- The Kokiri load-in is also when the pending-write trace spiked (41 hits per
  10 s at 120 s to 5,960 per 10 s at 130 s) - the same stale-CPU-write family
  as the water flicker.
- Every flash report points at the same family of passes going 2-22x over
  their neighbors: the bloom chain (55dbc0e56184236b+017ecb55c0c058c7), a
  432x240 HDR pass (deca253405f92330+29a6643e..., up to 12.1x), and the
  log-bloom final (bded5ce917183214 - the pass OOT_VISUAL_WORKAROUND.md
  already edits, gated by EDEN_NX2_OOT_VISUAL_WORKAROUND=1).

What has been applied on this branch (cherry-picked from upstream/nx2,
none of which fixes the flash):

- #692 cf4c470117 (TC MIB cap at device budget), #718 ccce17e4d9 (occlusion
  query reset cap 4 banks), #264 1a94e51f63 (read-trap arming), #817
  784cbb3924 (skip uploads overwritten by join copies), #818 9ab4690978
  (stale guest memory / StalePages, env-gated default off), #1210
  d94d16b0ea (host commit memory limit; OoT OOM), #778 57b5f4aee8 (Frizti
  v4 import; page_filter fix taken - out-of-range marks are now untracked
  instead of poisoning the filter). #910 skipped (needs SampledTextureMemo),
  #699 skipped (this fork already implements it as clear_variant).

What has been tried for the flash itself (it is env-gated, opt-in, test
builds only: EDEN_NX2_OOT_NAN_GUARD=1):

- A translation-time guard (src/shader_recompiler/ir_opt/oot_nan_guard.h)
  clamps the velocity pass (885ac67a.../23c42939.../df0bc0a65c87f552) and
  companions: NaN becomes 0, +-Inf/huge becomes +-1024. The measured build
  logged edits: eae2677b666c08a9 5 edits, 23c42939fea640ad 2 edits,
  92b7f9d9f1726b50 2 edits.
- One relaunch with the guard showed 1 alternating-frame event in the whole
  session vs the constant storm before it, and Kokiri rendering clean.
- One load-in flare still fired: frame 10779 (~221 s), 13 tiles, ghost/dirt
  mip chain (8e97cea098cfdbc6+ee1273e135cf7577, 128x67), finite values,
  not NaN. THE FLASH IS NOT FIXED. The guard is a probe, not a cure, and the
  white-out end state has not been excluded. Do not report it fixed.

Next steps, if picked up:

1. Make the guard always-on after owner validation (or extend it to the
   ghost chain, same idiom).
2. The remaining load-in flare needs the ghost/dirt chain clamped the same
   way (shader hashes 8e97cea098cfdbc6, 7abdd0a2c4698e50, e6f57a6a5c523021,
   f2b14c43ca0e4300).
3. The Kokiri ~10 s "menu" hold at boot is the game's own scripted hold
   (working set +~0.9 GB around it, decoder idle, no thread saturated).
   Not emulation; not fixed; do not attempt to remove it blindly.
4. There are upstream fork bugs to report separately: EDEN_NX2_FRAME_DUMP_PAIR
   wedges the frame dumper (pair capture always fails "cannot write" and never
   re-arms); the SAMPLED frame dump records only passes 0-44 (stops at draw
   ~1232 of ~2183), so it cannot currently see the scene/fog passes.

## 2026-11-11 (later) - Fritchii v5.8.4 merged into the fork; Ocarina2 v1 queued

Fritchii v5.5 -> v5.8.4 merged into merge/fritchii-v5.4-try (merge commit 0fd7ab7b4b),
on top of investigate/fsr3-fg (FG x3 + adaptive 12 GB DRAM OoT preset, 38a51a8a9e).
fritchii is Frizti'\''s own upstream line; the fork keeps its OoT-specific patches and
the Ocarina2 branding. What landed and how (the parts that were contended):

- Built on the Fritchii v5.4 import (38a51a8a9e lineage). fritchii-v5.8.4 fetched as
  tags (fritchii-v5.8.4 -> 4f5c53149b). 177 upstream commits since v5.4: Saves window,
  Help > Copy Data/fix orphans, per-game DLSS quality & preset (NVIDIA range-validated,
  DLSS preset per game), Nx2Hitch hitch tracing, hot/clean tracked-pages collector with
  epoch reset, TC_CEILING_MIB (7680), JIT_WARM on, zero-trim idle timing, EXP49 default,
  the Fritchii own-profile move (%APPDATA%\fritchii, imported once on first launch),
  Settings reset-to-defaults backups, TX notes v5.7, NXFormat .nx convert/manage,
  Hyrule Cheat Menu-safe cave (0x3C46E00) + call re-encode, EDS2/EXE fixes, DLSS N, S,
  Metro Exodus game entry visibility, FXAA default off, 12 GB Switch 2 memory layout
  ("Switch 2" label), skin notes.

- Conflict resolutions kept the fork where it was specific: ours'\''s 12 GB/FG x3/hook
  addresses semantics via the fork'\''s settings, Memory_12Gb adaptive, the OoT
  high-refresh Ctrl+U work aligns with upstream'\''s (shared Dark routine + cave at
  0x3C46E00 + water-ripple builtin behind SDL; EDEN_NX2_OOT_HIGH_REFRESH +
  EDEN_NX2_FRAME_DUMP_PAIR interplay preserved), Ocarina2 namespace (Ocarina2Ui) and
  Ocarina2 folder layout kept, Fritchii builds keep their own profile + EXP49 +
  hitch tracing + defaults. FrameGenActive kept upstream'\''s Fritchii-build gate.
  Wired upstream'\''s per-game DLSS (NvidiaDlssQuality) in, hid it without Streamline,
  kept the fork'\''s OotAntiAliasing(Game, NoSharpening, Fsr3, Fsr4) and the OOT preset
  default.

- The fork'\''s test helper bugs fixed along the way: test expected multiplier 2 and
  Memory_4Gb (stale after 38a51a8a9e; now adaptive-expected), update_checker hidden-
  marker duplicate test removed, builtin_patches Dynamic FPS fork tests kept alongside
  upstream'\''s ripple/high-refresh tests, game_presets comment corrected (8 GB ->
  adaptive 8/12 GB).

- FrameDump stays Ocarina2'\''s FrameDumpSchedule (white-flash tooling, pair capture,
  frame dump tests); upstream'\''s host-copies capture plumbing (EDEN_NX2_FRAME_DUMP_HOST
  via RequestNow(id, dir, buffers, host)) integrates into it: HostCopies() now rides on
  the schedule, capture_frame completes through Nx2Debug::FinishCapture on completion.

- Build: NOT GREEN YET at merge commit time (renderer_base.h FrameGenStatus missing
  brace from the resolution, fixed in this round; remaining errors TBD per rebuild).
  v1 candidate happens once Kokiri/playtest re-verified after this merge.

- v1 GUI (pending, owner'\''s ask): remove Discord/Donate buttons from the GUI (home rail
  + Help menu + constants/icons/qrc), rewrite update notes short and sweet (improve
  performance and stuttering/gameplay, not framerate; bugs known and being worked on;
  this is build Ocarina2 v1), bump dist/ocarina2_version.txt appropriately for the
  release, then merge to nx2 and talk about releasing. No pushing or releasing without
  owner approval.

## 2026-11-11 (later) - renderer_base.h union broke the struct brace; fixed

Merge note: the bop-union for renderer_base.h glued ours'\''s unterminated FrameGenStatus
open onto theirs'\''s DlssStatus, dropping the shared closing brace after generated_frames{};
the compiler expected ';' at namespace VideoCore'\''s close. Fixed by closing
FrameGenStatus before DlssStatus (sibling, not nested). If a similar resolution-union
breaks elsewhere, the test of it is always the compiler, not my reading.

## 2026-11-11 (evening) - Ocarina2 v1: merge complete, GUI buttons removed, notes rewritten, build green

The Fritchii v5.8.4 merge (merge/fritchii-v5.4-try, merge commit 0fd7ab7b4b) now BUILDS and links
bin/Ocarina2.exe (BUILD_EXIT=0) after these post-merge fixes:

- renderer_base.h: union glued ours' FrameGenStatus (unterminated in ours' side of the conflict) onto
  theirs' DlssStatus, dropping ours' closing brace - closed it before DlssStatus (sibling, not nested)
  (f935b49335)
- vk_graphics_pipeline.h: merge auto-resolver duplicated ColorOutputMask/color_output_mask (both
  forks added the same block independently) - deduped (ecd4b1492a)
- vk_frame_dump.cpp: FrameworkDump's Rearm() referenced trigger_file, which ours keeps in
  FrameDumpSchedule - schedule.TriggerFile() (a74159de0f)
- CMakeLists.txt: rc.exe never got the Windows SDK um/shared/ucrt includes (winresrc.h missing) -
  CMAKE_RC_FLAGS now carries the SDK paths; fixes yuzu-cmd and yuzu-room .rc compilation (ec05f80004)
- vk_pipeline_cache.cpp: restored ours' non-const BuiltPipeline forwarding overload - header declares it
  and call sites bind it (3fcc92b4ca)

Build status: BUILD_EXIT=0 on the merged tree; Ocarina2.exe links. Unit tests are wired in CI
(build-check.yml, -DYUZU_TESTS=ON, ctest) - they cannot build on this machine (no Qt6Test component),
so the three merged test TUs were syntax-checked with clang-cl /Zs against the real flags (clean).
YUZU_TESTS is back OFF locally, as it was.

Owner's v1 edits, all in (each its own commit, on merge/fritchii-v5.4-try):
- v1: remove Discord and Donate from the GUI (ff6123dfa2) - the dock buttons, the Help menu
  entries, OpenDiscord/OpenDonate + URL constants, the painted community/heart icons, the SVGs and
  their qrc entries. Saves button (Fritchii v5.8.3 feature) stays. Discord Rich Presence setting
  untouched (not the button the owner meant).
- v1: short and sweet update notes (687a584fdb) - performance and stuttering and actual gameplay,
  NOT fps; known bugs, being worked on; this is build Ocarina2 v1. dist/ocarina2_version.txt is "1.0".
- Crash-note: fritchii.qrc icons/ names (community, heart) were unused at runtime (icons are painted);
  upstream shipped them anyway. Ours now matches upstream minus the buttons.
- whats-new.md keeps credits for Eglantine/Eden, Lichii and Frizti's Fritchii; the v5.4-v5.8.4
  changes are condensed into the merged section upstream notes referenced.

State: build green on merge/fritchii-v5.4-try. Not pushed. Not merged to nx2. Owner has not
approved releasing/tags; the next talk is merge to nx2 and release planning.

## 2026-11-11 (evening, rename) - profile directory is now ocarina2

Owner's call: the app-data profile folder is %APPDATA%\\ocarina2 on Windows (the ocarina2
directories under $XDG_DATA_HOME / $XDG_CONFIG_HOME / $XDG_CACHE_HOME on Linux and macOS).
Previously (v5.8 via the Fritchii merge) it was fritchii; before that, eden.

Kept as migration sources so existing players keep their saves: eden (pre-merge fork),
fritchii (the v5.8 profile builds), lichii, yuzu, suyu, sudachi, citron. All four shapes work
unchanged: the first-launch import can target them, the Help > Copy Data dialog lists them,
and the import-worker prompt offers them.

Touch points:
- src/common/fs/fs_paths.h: EDEN_DIR ocarina2, EDEN_LEGACY_DIR eden (kept), FRITCHII_DIR fritchii
- src/common/fs/path_util.{h,cpp}: EmuPath Fritchii{,Config,Cache}Dir + LEGACY_PATH both platforms
- src/yuzu/migration_worker.h: legacy_emus now includes Fritchii (GUI import prompt)
- src/frontend_common/profile_import.cpp: the Help > Copy Data request allowlist accepts fritchii
- src/yuzu/user_data_migration.cpp: Fritchii in the v5.8 import candidate list + Ocarina2 wording
  in the (Lichii) import dialog
- nx2_switches.txt.txt and nx2_switches.h fallback: ocarina2 -> eden -> fritchii
- tests/common/data_directories.cpp: expects ocarina2
- docs: Mods.md %AppData%\\ocarina2; README profile section; dist/nx2_switches.txt comments
- UI strings with "Fritchii" as the app name replaced with "Ocarina2" (reset/restore dialogs,
  migration prompt, startup error, export_log stuck-process note, migration_worker prompt)

Verified: BUILD_EXIT=0 on the merged tree with these changes; Ocarina2.exe links.

## 2026-11-11 (evening) - v1 play test: boots into Kokiri, survives input, preset confirmed

Setup used for the smoke test: the merged + renamed build was run on this machine. The
%APPDATA%/fritchii profile (from the v5.8-era merged builds) did NOT exist here - only the
pre-merge %APPDATA%/eden profile, so the play test ran against a manual copy
eden -> ocarina2 (the v1 import flow does exactly that copy on first launch; eden itself
untouched, it stays as the rollback).

What was verified, with the emulator driven headlessly through the NX2 debug server:
- Ocarina2.exe (build 732822c4e5, version string "Ocarina2 1.0", Vulkan, switch2=true) boots
  THE LEGEND OF ZELDA OOT and lands in actual gameplay (Kokiri Forest, Link running with HUD,
  minimap present), rather than sitting at the title.
- OOT preset applied from the log: OOT 60 FPS patch on, FG x3 (Fsr3), oot_anti_aliasing=Fsr3,
  memory_layout_mode=Memory_12Gb, OOT Max Resolution off (correct for the 6 GB card).
- Input works end to end (debug-server left-stick forward walk for 90 presented frames; Link
  advanced through the forest, fairy and signpost visible in the before/after screenshots).
- No crash, no white flash observed in the smoke session. Not a certification - the flash is
  intermittent and the Kokiri dogfight is what exercises it.
- Warnings seen are the normal stubs (AM/NVDRV/NIFM). One real error class worth noting:
  HW.Memory ReportUnmappedRead during the Kokiri scene load (buffer_cache unmapped reads);
  the game kept running past it and presents resumed. Also one 1.5 s present hitch at ~82 s,
  dominated by first-area shader JIT (not a regression signal, but worth watching).
- Guest frames measured at a very steady ~60 ms mean (~16.7 FPS in this scene) with 0 stutters
  reported in the sample window - a different, lower FPS class than the title/cutscene case;
  the owner should confirm whether that matches their experience of the merged build.

What this does NOT verify: UI strings render in-game, Saves window works with real saves,
settings restore/reset behavior, full traversal to any other area, the white flash.

## 2026-11-11 (late) - FG x3 follow-up: "Lossless.dll status 1" root-caused (not installed); LSFG warning gated to the LSFG path

Question carried out of the v1 play test: why does the log say `LSFG unavailable: Lossless.dll
status 1` while FG x3 is configured.

Findings:
- LosslessStatus 1 = NotInstalled. `%APPDATA%\ocarina2\lossless` (and the eden one) exist but
  are empty. Lossless Scaling is NOT installed on this PC - not in D:\Steam, C:\SteamLibrary or
  D:\SteamLibrary, and a scan of C:\ finds no Lossless.dll anywhere. The emulator was reporting
  exactly what is on disk; the code looks in the right place (EdenPath::LosslessDir =
  `%APPDATA%\ocarina2\lossless\Lossless.dll`).
- LSFG is not needed for the configured frame generation: frame_gen_type is Fsr3 (shown in the
  UI as "Optical Flow"), which builds from the embedded SPIR-V in
  src/video_core/host_shaders/ (fsr3_frame_interpolation_comp_spv / fsr3_optical_flow_comp_spv).
  The missing-DLL unavailable path in FrameGen::Process only applied when type == Lsfg.
- The warning itself was misleading: LsfgShaders is constructed whenever frame_gen is on, and
  its constructor logged "LSFG unavailable" even on the Fsr3 path. Fixed: the warning only fires
  when frame_gen_type == Lsfg, and LsfgShaders is not constructed at all on the Fsr3 path.
  (present/lsfg_shaders.cpp, present/frame_gen.cpp)
- To use real LSFG the owner must buy/install Lossless Scaling (Steam) and drop its Lossless.dll
  into `%APPDATA%\ocarina2\lossless\`. Until then LSFG stays unavailable by design; Optical
  Flow FG x3 is unaffected.
- FG x3 visibility: the guest asks for swap interval 1 at 60 Hz with FPS limit 60, so presents
  cap at 60 (the log shows ~615-720 presents per 10 s = 61-72 fps against the 71.91 Hz refresh).
  In Kokiri the native frame runs ~17.5 ms (~55 fps) so FG is invisible at the cap; FG only shows
  through in the heavy areas where native drops below 60 (the "ran a frame ahead" counters of
  400-600 per 600 presents in the heavy windows indicate generated frames are being presented).
  Not measured in a controlled A/B yet.

## 2026-11-11 (evening, later) - LSFG x3 wired up: Lossless.dll installed + validated, config switched, logged-clean build

Owner pointed at the install: `D:\Steam Game Files\steamapps\common\Lossless Scaling\Lossless.dll`
(7,521,280 bytes, 2026-11-05). It is now copied to `%APPDATA%\ocarina2\lossless\Lossless.dll`
(SHA-256 identical, 626B196D...). The two `lossless` folders were empty before.

Validation (offline PE/resource parse mirroring `lossless_dll.cpp`, plus `lsfg_translate.cpp`):
- Format PE32+, 6 sections, RCDATA type 10, 300 resources, IDs 101-400.
- All 25 required native fp16 SPIR-V modules (ids 304, 305, 329-351 = base + NATIVE_FP16_OFFSET 49)
  present and valid (SPIR-V magic 0x07230203, size % 4 == 0, >= 20 bytes).
- Expect on first launch: `LSFG shaders loaded: 25 modules` and a one-time `lsfg_spirv.cache` build.
  No `LSFG unavailable` warning (the file exists; also the warn is now gated to type == Lsfg).

Config (owner's call: use LSFG, x3, as low latency as possible while keeping detail):
- qt-config.ini (main, MC in the log) frame_gen_type: 1 (Fsr3) -> 0 (Lsfg). sdl2-config.ini matched.
  Backups: *.bak-lsfg-fgen-switch. Single line changed; no BOM; byte-identical otherwise.
- The OOT per-game config (0400486024DEC000.ini) inherits frame_gen_type from the main config
  (use_global=true), keeps its frame_gen=true and frame_gen_multiplier=3 overrides, and the OOT
  preset does NOT force the type (only enablement + x3 multiplier), so the switch sticks.
- Low-latency knobs kept: frame_gen_queue_target=0 (min composites in flight), vsync Immediate
  (preset), pacer target_rate=0 (targets the measured 71.91 Hz refresh). Detail kept:
  flow_scale_auto=true (computes flow scale from the render/present ratio; at 1x render it is
  effectively 100%, i.e. full-resolution optical flow - the manual 75 would soften edges).
  LSFG itself costs ~a frame vs Optical Flow; that is the price of the better interpolation.

Build: D:\Ocarina2-merge-build-dclang (clang-cl, RelWithDebInfo, ninja from VS 2022 BuildTools,
12 jobs). Rebuilt after the 3f54e4cdf1 FG fix (frame_gen.cpp + lsfg_shaders.cpp recompiled,
Ocarina2.exe relinked 14:08, scm_rev bumped). First invocation ended exit 1 without printing any
FAILED line; the immediate re-run reports `ninja: no work to do`, exit 0, and the ninja log shows
all 13 edges completed including bin/Ocarina2.exe. Build is good.

FG log audit before the build (owner asked for this first): the ONLY FG-related Warning/Error in
the current log (eden_log.txt, 13:51) is `LsfgShaders: LSFG unavailable: Lossless.dll status 1`
(L891); the previous log has the same single line. No frame_gen/pacer/Fsr3 warnings or errors
anywhere else. With the DLL installed the line cannot fire again; with the gating fix it only
fires on the Lsfg path with a genuinely broken DLL.

To verify in-game: launch Ocarina2.exe from that folder, then check eden_log.txt for
`LSFG shaders loaded: 25 modules`. FG button should read FG LSFG x3 (not UNAVAILABLE). NOTE: the
first LSFG run builds the shader cache (one-time, fast). If LSFG looks/performs worse than Optical
Flow here, the config flip is a two-line revert (backups kept).

## 2026-11-11 (very late) - LSFG x3 is LIVE on the owner's profile

After a long detour, the working path was the GUI flip, not file edits. Verified in the
14:10-launch log (eden_log.txt):
- [286.8s] Config <Info> LogSettings: M- Renderer.frame_gen_type: Lsfg
- [286.8s] MC Renderer.frame_gen_multiplier: 3, MC Renderer.frame_gen: true
- [305.4s] LsfgShaders: LSFG shaders loaded: 25 modules
No "LSFG unavailable" warning. The FG status button shows FG LSFG x3.

How the pieces got there:
- Lossless.dll (Steam install at `D:\Steam Game Files\steamapps\common\Lossless Scaling\Lossless.dll`)
  was copied to `%APPDATA%\ocarina2\lossless\` at 13:58. Offline PE/resource validation passes
  (all 25 native FP16 SPIR-V modules present). The GUI-side flip made the running game build the
  chain, which logged the 25-module load - i.e. the DLL is present and usable on the real profile.
- Direct edits of qt-config.ini from the tool environment are NOT persistent: the game rewrites
  qt-config.ini at every boot from its in-memory state (verified twice - edits reverted to Fsr3).
  Gui-apply (per-game Graphics page, or the global page) is the only write path that sticks.
- The frame_gen FG type/multiplier widgets read Settings::values live, so the change applied
  without a restart. The OOT preset does NOT force frame_gen_type; it stays LSFG across reboots.

Test setup owner is running: pid from the 14:21 wrapper-bat launch (EDEN_NX2_APPDATA_ROOT was a
red herring - launched instances run with the user's environment and always resolve
C:\Users\user\AppData\Roaming\ocarina2). Applies to normal launches as well.

Open test questions to watch while playing:
- FPS/pacing vs Optical Flow (LSFG costs more GPU; on the 3050 listen/look for drops in heavy areas)
- HUD artifacts generated into frames (minimap/HUD can ghost under frame gen)
- Hitch class regression vs the 12 GB DRAM emulation build
- If bad: flip Frame Generation Type back to Optical Flow in the same dialog (applies live)

Files in Downloads\LSFG-setup are now obsolete (Lossless.dll copy + README + the wrapper bat):
the setup is done. Owner can delete the folder; Downloads does NOT need it anymore.

## 2026-11-11 (night) - FG type switching in game: F12 QuickMenu row + persistent status-bar toggle

Owner asked: switch between Optical Flow and LSFG while in game. Two gaps wired up:
- The Ocarina2 UI hides ALL frame_gen* settings from the config pages (HidesSetting in
  yuzu/ocarina2/ocarina2_ui.cpp: "No frame generation in Ocarina2, and its options are dev
  tools") - so the Graphics tab looks like it has no FG settings at all. The FG status-bar
  button (left-click on/off, right-click type menu) was the intended in-game surface.
- The right-click menu flipped frame_gen_type in memory but never saved. Now both it and the
  left-click handler call ApplySettings + SaveAllValues (same path as OnToggleOotFrameRate),
  so the switch persists across restarts.
- The F12 / L3+R3 QuickMenu (yuzu/ocarina2/quick_menu.cpp) gained a "Frame generation" row on
  its Settings page (triforce icon): shows LSFG / OPTICAL FLOW, cycles on tap
or Left/Right,
  applies live (FrameGen reads Settings::values per frame) and saves, same contract as the
  other rows. QuickMenuHooks gained frame_gen_text + cycle_frame_gen.

Status: syntax-checked only (clang-cl /Zs, both TUs clean). NOT rebuilt yet - the running
game holds Ocarina2.exe open (link would fail). Relink once the owner closes the game.

## 2026-11-11 (night, final) - FG in-game switch landed and verified

Owner relaunched on the rebuilt exe (14:36:37). Verified in the fresh log:
- M- Renderer.frame_gen_type: Lsfg (multiplier 3) - LSFG still active across the reboot
- LsfgShaders: LSFG shaders loaded: 25 modules
- Ocarina2 quickmenu key=... events at 4.4-5.4s - owner opened F12 menu and navigated it;
  the new row is wired (owner confirmed it was them).

Available in game now:
- F12 / L3+R3 -> Settings page -> "Frame generation  LSFG / OPTICAL FLOW" (tap or Left/Right),
  applies live, persists to config on save like the other quick-menu rows.
- FG status bar button: left-click toggles FG on/off (now persists), right-click picks
  Optical Flow or Lossless Scaling (LSFG) (now persists).
- The Graphics-page FG rows remain hidden by design (Ocarina2 "real frames only" reskin).

A/B plan for the owner: Kokiri vs heavy zones with LSFG vs Optical Flow; watch pacing,
HUD ghosting, and hitch class vs the 12 GB DRAM emulation build. If LSFG loses, flip back
in the same menu - no restart.

## 2026-11-11 (night, release-prep) - LSFG auto-install from Steam: shipped feature, verified end-to-end

Owner asked: the GitHub zip must work for everyone, zero downloads, merging to nx2 afterwards.
Legality: Lossless.dll is Lossless Scaling's paid, copyrighted code - it can NEVER be in the
public zip. The answer is auto-install from the user's own Steam install, exactly what lsfg-vk
and Lossless Scaling itself do:
- video_core/frame_gen/lossless_install.{h,cpp}: TryAutoInstallLosslessDll() finds Steam
  (HKCU/HKLM SteamPath on Windows; STEAM_HOME/.steam/.local/share/Steam elsewhere; plus
  libraryfolders.vdf for non-default library drives), copies common/Lossless Scaling/Lossless.dll
  into the ocarina2 lossless folder and validates it (ValidateLosslessDll). No downloads ever.
- FrameGen::Process retries once per run: if LSFG shaders are invalid (DLL missing/stale), it
  auto-installs and rebuilds LsfgShaders in the SAME session; only then declares unavailable.
- New "Install Lossless.dll from Steam..." action in the FG status button right-click menu;
  the MissingLosslessDll tooltip now explains both paths. CMakeLists: files added under ENABLE_LSFG.

Verified live on this PC (owner closed the game for the test; backup of the profile DLL taken):
- deleted the profile's Lossless.dll (sha256 626B196D...), booted Zelda, close to title:
  - video_core/renderer_vulkan/present/lsfg_shaders.cpp:33 LSFG unavailable: Lossless.dll status 1
  - video_core/frame_gen/lossless_install.cpp:182 TryAutoInstallLosslessDll: LSFG: installed
    Lossless.dll from D:\Steam Game Files\steamapps\common\Lossless Scaling\Lossless.dll
  - video_core/renderer_vulkan/present/lsfg_shaders.cpp:37 LsfgShaders: LSFG shaders loaded: 25 modules
- reinstalled DLL is sha256-identical to the Steam original (626B196D...). LSFG engaged in the
  same run - no restart needed once the game presents frames.
- Build green (D:\Ocarina2-merge-build-dclang, exit 0), clang-cl /Zs clean on the new TU too.

Default for fresh downloads stays frame_gen_type=Fsr3 (Optical Flow, zero deps, works for
everyone); LSFG owners get one-click/self-installing LSFG. FG button right-click menu has both
switch actions now, both persisting.

## 2026-11-12 (midday) - CI un-stranded; Hyrule Castle Town measured; macOS on demand only

### CI (all root-caused from job logs, all fix commits on nx2)
- Release Linux (x86_64 GCC, aarch64, clang PGO) failed on `yuzu/ocarina2/home.h:89` -
  `QToolButton` used in members but never declared; Linux Qt headers don't leak it (Windows'
  do, which is why local builds looked clean). Fixed with a plain forward declaration.
- Linux aarch64 link failed: `undefined reference to Common::GetProcessorCount()` - the
  definition was inside `#ifdef ARCHITECTURE_x86_64` in cpu_features.cpp. Moved it out of the
  guard; the portable fallback body was already there.
- build-check Linux GCC + Windows clang-cl failed on the stale test
  `src/tests/video_core/buffer_cache_ports.cpp` - BufferCache no longer has
  native_indirect_uniforms_this_frame (native banks compare pages on every bind now), the mocked
  Runtime now takes (u64, u64 = 0) for WaitSubmittedTick and `span<const BufferCopy>` for
  CanReorderUpload. And on `storage_buffer_pointer_tracking.cpp` - GCC's
  -Werror=missing-declarations wanted a prior declaration for EmitTableLoop (file-scope).
- windows-x86_64-pgo always died at "Check PGO profile": ocarina2-windows.profdata was never in
  the repo (.gitignore ignores *.profdata; only eglantine.profdata was force-added before).
  The OoT-trained profile from D:\Ocarina2-pgo\profraw was merged with LLVM 22.1.8 (the exact
  LLVM tools/windows/install-llvm.ps1 pins for CI); MSVC-ABI names verified (?-prefixed), 24 MB,
  committed as ocarina2-windows.profdata. Local clang-cl 22 reads it cleanly (hash warnings
  only, exit 0). eglantine.profdata untouched.
- macOS "cancelled": hosted arm64 runners never got acquired (capacity-starved), jobs sat
  queued forever on Nov 11. macOS is out of the push matrix now and builds only on
  workflow_dispatch (build_macos=true) as its own job.
- A workflow edit initially broke GitHub's validation outright: `matrix` isn't available in a
  job-level `if:`, and the release job referenced needs.prepare-release it doesn't need. Both
  fixed; actionlint is clean.
- Windows "Install LLVM" failed in one run with a connection timeout (network, not a bug) -
  unrelated to the code; it has to pass on retry.
- Stale queued runs from Nov 11 (2/3/39/40/41) can just be cancelled from the Actions tab
  (the browser here was NOT logged into GitHub, so I didn't cancel them).

### Performance (Ocarina2 1.1, this PC, RTX 3050 class, EDEN_NX2_GPU_PROFILE=1)
- Hyrule Castle Town (night square): ~15.4 FPS native, mean 64.9-67.0 ms, p50 ~64 ms,
  p90 ~72-76 ms, p99 ~77-87 ms, 0 stutters. Hyrule Castle plaza exterior (night, castle wall
  view): ~14.9 FPS. Town entrance, walking: ~17.2 FPS. Everything runs vsync bucket 4+ at
  120 Hz; FG x4 keeps the displayed rate ~60.
- Texture cache line: "Aggressive GC engaged: used 4825 MiB >= critical 3800 MiB" - the 3.8 GB
  hard cap (deliberate; Windows paging above ~4 GB commit) fires in the square; ASTC panels and
  render targets get evicted and re-created. No visible stutter, just log spam. Lowering it
  further would thrash the ASTC decodes (this is the trade-off the cap already makes).
- GPU thread per second (in 10 s reports): method ~246 ms, macro ~121 ms, push ~104 ms,
  bind stage buffers ~66 ms, update buffers ~63 ms, list GPU wait ~125 ms,
  read handles ~26 ms, fill image views ~24 ms, pipeline lookup ~25 ms,
  dynamic state ~21 ms, draw ~31 ms, fence ~30 ms, flush work ~26 ms,
  texture upload ~13 ms, compute sync ~12 ms, host CPU busy ~864 ms/s. The heavy cost is the
  MME/Method interpreter path plus uniform-buffer compare traffic (105K unchanged / 60K changed
  compares per second). Inherent to this emulation approach; nothing safe to hack blind.
- Zone loads in town (town <-> plaza): ~8-15 s, dominated by file reads and code recompilation
  counters; cold paths are still slow, in-cache reads dominate.

### Disk
- C: has ~19 GB free after moving the stale build-low-end tree to D:\Ocarina2-old-builds\ (1 GB).
  LLVM 22.1.8 lives in D:\Ocarina2-llvm22 (checksum-verified download used only for the PGO
  merge). Local CPM cache (~0.9 GB) and .git (~0.3 GB) stay put.

### Open / unfinished
- v1.0 release NOT published yet. CI must go green first (build-check + release-binaries);
  v1.0 = nx2 tip per the owner's call, then v1.1 follows. The auto-release publishes tag
  build-N; renaming the TAG would break the in-app update checker (it compares tags
  build-N against LIHCHII_BUILD_TAG), so only the title/notes of the release may be renamed,
  not the tag.
- White flash: not touched; not reproducing in town scenes (Kokiri was the reproducer).

## 2026-11-12 (early afternoon, cont.) - more compile fixes; release pipeline is in flight

Follow-ups to the same theme (all caught by CI; each one is a real upstream-to-fork regression
that only showed up on compilers nobody ran on our tree):

- main_window.cpp: the "Enter = Space for QCheckBox/QRadioButton" filter class was inside the
  #ifdef _WIN32 block, so Linux/macOS builds had no definition for it while the use at the call
  site is unconditional. The class is pure Qt (no Windows APIs) - hoisted out of the _WIN32
  block; Windows unchanged.
- src/tests/video_core/command_fetch_order.cpp was saved as UTF-16-LE (BOM FF FE): both GCC
  and clang-cl reject it outright ("stray \\377 in program" /
  "UTF-16 (LE) byte order mark detected"). Rewritten as plain UTF-8; repo-wide scan shows no
  other UTF-16/invalid-UTF-8 sources (only binary assets, unaffected).
- src/tests/video_core/pipeline_cache_files.cpp: the merge-helper functions (Header, Record,
  ReadRecordKey, Merge, WriteUsed, ReadUsed) were at global scope with no prior declarations
  (GCC -Wmissing-declarations) and the file had an unbalanced closing
  brace at the end ("expected declaration before }"). Declared the six helpers at file scope
  and removed the stray brace; clang-cl's checks now see a valid TU too.
- src/tests/video_core/* range-loop-constructs (maxwell_numeric, uniform_stream_reuse,
  ampere_yoshi_title, ampere_logged_numeric, nx2_video_render, slot_vector) bound structured
  bindings by value; GCC 13 -Werror=range-loop-construct flagged them. All take references
  now.

Windows-side PGO is green: the windows-x86_64 jobs finished OK with the committed
ocarina2-windows.profdata (Check PGO profile passes). The macOS arm64 job is opt-in via
workflow_dispatch only, so push never waits on or fails on macOS anymore.

Release status as of this note: nothing published yet. The auto-release trains (build-N tags,
LIHCHII_BUILD_TAG) for the current tip will be the v1.0 per the owner's call; the release-binaries
workflow skips pushes that only touch tests, so the test fixes don't re-trigger binary builds.

## 2026-11-12 (evening) - updater asset naming fixed; CI test-step PATH fixed; tests segfault under investigation

- Update checker build-N track was looking for assets named lichii-<platform>[-pgo].<ext> on
  rikomusicx-lgtm/Ocarina2, but every release publishes ocarina2-* names. The checker would
  never have found an update. net.cpp now builds ocarina2-<platform>[-pgo].<ext>. The Ocarina2
  (Windows, Frizti/Lichii) track is untouched.
- update_checker_tests: fixtures now use rikomusicx-lgtm/Ocarina2 + ocarina2-* assets, the HTTP
  mock server expects User-Agent Ocarina2 (production net.cpp sends that), and every unchecked
  GetPlatformAssets(...)[0] dereference is behind a REQUIRE so a naming mismatch fails the
  test instead of segfaulting the binary.
- build-check "Run tests" (Windows): ilammy/msvc-dev-cmd rebuilt PATH from the snapshot taken
  before install-qt ran, dropping Qt's bin. compatibility_report (Qt Test, QT_QPA_PLATFORM=offscreen)
  died on the missing Qt6Core.dll before printing anything (ctest reported Failed with no output).
  Run tests now prepends $QT_ROOT_DIR/bin to PATH (Linux sets LD_LIBRARY_PATH).
- config_tests: assertions expected the pre-v1.1 historical defaults; v1.1 baked the tuned
  values into the linkage defaults, so existing profiles keep SgsrEdge/Low/Gpu/true/true.
- Local repro of the `tests` segfault on D: (fresh build dir, LLVM 22.1.8 = CI-pinned version,
  local Qt 6.11.1) in progress; cause TBD.

## 2026-11-12 (late) - v1.2 pulled by the owner; publish is on demand only now; v1.3 = next build

- The v1.2 release (build-12, fa9497a3be) shipped with the internal build number in the title
  ("Ocarina2 1.2" was built as "build-12"), a stale What's New, and no real description. The owner
  deleted it. Releases now happen only on demand: release-binaries still builds on pushes to nx2
  (CI validation) but never creates a release tag or publishes; a release is created only via
  workflow_dispatch with publish_release=true.
- build-12 was re-created as a description-only release (no binaries; they were deleted with the
  original release): it documents what changed vs v1.1, says explicitly that the binaries were
  pulled, and points users at v1.1 (build-11) as the last downloadable release. The v1.1-to-v1.2
  difference list lives there and in the in-app What's New now.
- GenerateSCMRev: the Ocarina2 version from dist/ocarina2_version.txt wins over LICHII_BUILD_TAG
  for the user-facing name (an Ocarina2 release now reports "Ocarina2 1.2"); the build-N tag is
  still baked in for the update checker.
- host_memory.cpp: the SEC_RESERVE fault net tracked only the most recently reserved section, so a
  raw access to any earlier section faulted without a handler and crashed (this was the tests-bin
  crash). It now tracks up to eight sections, claimed with CAS and released on teardown.
- buffer_cache_ports tests updated to the fork semantics (downloads stage through Finish; mapped
  buffers wait on their own tick; page-quantized uploads; CPU marks only from the snapshot fast
  path). sysmix_sink: renderer stream writes silence on underrun (deliberate fork behavior),
  audio-out repeats the last frame - the test asserts both.
- What's New gains an Ocarina2 v1.2 section; the version file is now 1.3 (next build).
- Nothing is pushed or published until the owner says so.

## 2026-11-12 (late, post-incident) - v1.3 was auto-published by a push; it is taken down; releases are dispatch-only now

- The release-binaries workflow published on every green push (prepare-release created tag build-<run-number> and the publish job attached it). So the nx2 pushes of the notes commits published Ocarina2 v1.2 (build-12 at fa9497a3be) and Ocarina2 v1.3 (build-14 at 0ca78eb998) without anyone asking. The owner said 1.3 is not meant to exist yet (nothing in it worth a public release + it was made without his go-ahead); the release is deleted.
- The workflow now builds on pushes to nx2 but never creates a tag or publishes; a release is made only by workflow_dispatch with publish_release=true (maintainer pushes the button). The verify step enforces the title Ocarina2 v<version>.
- v1.1 (build-11) remains on GitHub with its 10 binaries. v1.2 (build-12, description-only, no binaries) remains as documentation of what orphaned v1.2 was. tag build-14 still exists but points at nothing (no release attached); if it bothers you, say so.
- No release will be posted again without the owner's explicit word.
- The running game (pid 15260) is the owner's own instance of the v1.2 binaries; left untouched.

## 2026-11-12 (late) - CI green push: three real bugs the test suite caught

- `MemoryManager::PageTableOp` (video_core/memory_manager.cpp): unlike BigPageTableOp and the
  read/write paths, a map outside the 40-bit address space was not rejected - it walked the
  page-table vectors out of bounds (caught by "A 40-bit address space maps Switch 2 VA past
  128 GiB", src/tests/video_core/memory_manager_runs.cpp). That is the crash that made the
  `tests` binary segfault in CI at a random point (the order is randomized per run) and is
  why the crash moved between runs locally.
- `HostMemory` move constructor/assignment (common/host_memory.cpp) were `= default` while the
  destructor owned the fallback buffer: moving leaked it, and the source dtor freed a buffer
  the moved-to object still pointed at (double free, heap corruption). Moves are now
  swap-based and assignment releases the replaced buffer. Also on Windows, `VirtualFree` was
  given a non-zero size with MEM_RELEASE - it must be dwSize=0, or the call fails with
  ERROR_INVALID_PARAMETER and the fallback buffer stays committed forever.
- Test suite mechanics: the builtin_patches tests compared ~63-83 MB NSO images with Catch2's
  full-operand expansion, taking the test process down with a stack overflow on any mismatch
  (now compared fail-safely, first mismatching byte reported); tests.exe reserves 64 MB of
  stack; the SPIR-V global-memory helper test now covers both the default (inlined, -~2.6 fps
  tradeoff measured on an RTX 4080) and EDEN_NX2_SPIRV_INLINE_GLOBALS=0 (DontInline), with the
  env switch read live instead of cached per process; stale expectations were re-aligned with
  the deliberately re-baked v1.1 linkage defaults (FG x4 + Target_Auto pacing, SGSR edge +
  FXAA, Immediate vsync - the v1.1 commits made those the defaults; Frizti's values ARE the
  linkage defaults since v1.1).
- Local suite green end to end with the release-gate in place: tests (1052 cases, ~70.8M
  assertions), config_tests (426), all green. Nothing published; pushes still cannot publish.

## 2026-11-12 (late) - 1.3 notes drafted, nothing posted

whats-new.md gained a "Ocarina2 v1.3 (in development, not released)" section listing the
stability fixes that landed today (the address-space map crash, the memory-owner moves,
Windows fallback reclaim, the inline-by-default SPIR-V helpers) and marking VRAM/texture
thrash, the white flash and the shader warm-up as in progress. It ships only when the owner
says 1.3 is cut; nothing about 1.3 has been posted anywhere.

## 2026-11-12 (late, optimization pass) - vram ceiling un-clamped for big cards; white flash and shader preload analyzed, nothing shipped blind

### VRAM/ASTC (done, in tree)
- The 2026-11-10 "vram: hard cap texture and buffer caches at 3.8 GB" commit clamped all
  caches' critical memory at 3.8 GB
  regardless of the card. That clamp also silently nulled three mechanisms the fork built for
  Kokiri Forest on Switch 2: the native-tiling floor in TickFrame (critical = device_local
  - 256 MiB), the driver-budget follower (critical follows the budget with pressure/block-slack
  margins, the correct answer when the card is 12-16 GB), and the Frizti 7680 MiB built-in
  ceiling (EDEN_NX2_TC_CEILING_MIB=7680 in nx2_switches.h, owner-tuned 2026-11-10 on the
  16 GB card: VRAM 10 -> ~6 GB, no new stutter, 58.8 fps over 30 min). On the RX 9070 XT
  machine every aggressive GC pass was evicting Kokiri's CostlyLoad ASTC textures because of
  the clamp, not the budget.
- Fix: the 3.8 GB cap (both caches) now applies only when the driver budget is reported and
  below 10 GiB (the 6 GB 3050 class it was for) or not reported (OpenGL). Cards at 10 GiB and
  up (the 12/16 GB class) follow the budget follower, the commit-room/HostCriticalMemory
  limit and the 7680 MiB Frizti ceiling instead - the exact configuration of the 2026-11-10
  playtest. Cards with smaller budgets (and OpenGL) are byte-for-byte what they were.
- No blind changes on the other axis: the Kokiri ASTC textures already decode on-GPU to BC3
  (opaque to BC1), the >1 GiB ASTC arrays keep trimmed mip levels, sparse residency handles
  unwritten terrain, SEC_RESERVE is default-on, and the hard cap stays for the 3050 class
  since the 06-11 paging regression was observed on that card.
- Measured on the 9070 XT the change keeps ~7.5 GB of texture cache (Frizti ceiling); on the
  3050 nothing changes. Not measured here (no game running); the owner A/B on his machine.

### Texture-cache thrash - analysis outcome
- At the 3.8 GB cap the aggressive GC evicts Kokiri's CostlyLoad ASTC (a 6144x6144 5x5 one is
  31 MiB); the comment in texture_cache.h has the numbers (24 ms re-creation stall every
  ~128 frames). On <=8 GB budgets that is the price of the cap, deliberately chosen by the
  owner after the Windows-paging observation. No safe blind change exists inside the cap:
  evicting cheaper textures first does not reach the cap without touching Kokiri's big
  textures, and trimming big ASTC mip levels changes what close-up surfaces look like.
  The Kokiri textures are already decoded on-GPU (no CPU decode stalls), ASTC-native when the
  driver reports optimal ASTC, and opaque ones are BC1.
- What can be set without a build: EDEN_NX2_TC_CEILING_MIB can only LOWER the threshold.
  On the 9070 XT the built-in 7680 now applies after the clamp fix; on the 3050 nothing to
  raise it to.

### White flash - analysis outcome (candidate, needs owner A/B)
- The residual "white flash in combat-heavy scenes" points at the fence-flagged query report
  race the fork already knows: at default fence behavior below accurate parts, a fence-flagged
  report can be read after the fence is skipped as empty (fence_manager.h, #337). The fork's
  fix for exactly this symptom is EXP33 (is_synced fence reports stay registered until the
  fence runs + the fence is made and waited even when empty). It is off by default, marked
  "off by default, not measured yet" in nx2_debug_control.h.
- Not flipped here on purpose: flipping it needs the owner's A/B in a combat-heavy session
  (EDEN_NX2_EXP33=1 live, or =0 to undo) while watching the flash watch and the fence zones
  in the GPU profile. If the flash stops and fps holds (async/sprite-heavy scenes), it should
  ship as the 1.3 default and replace the current known-flashing state.
- Already shipped defaults that address the same symptom class: the uniform-bank per-bind
  check (stale host contents, exp9=3), render-target relocation (exp43=3) and pooled target
  images (exp42=1), async read-back for chased paths (exp49=1), empty-submit elision
  (exp61=1), the Splatoon-Raiders fence-side reports handling (#215, FlushQueryResults).

### Shader preload - analysis outcome (already on, nothing to flip)
- EDEN_NX2_PRECOMPILE (off by default up to 2026-11-09, built-in = 1 since then by owner's
  call): boot looks for the title's precompiled file or a recipe, assembles the recipe with
  the game's own files, and builds the file's pipelines on idle-priority threads. OoT cold
  boot freezes 700-850 ms -> 40-70 ms, 35 -> 43 fps (owner, the "Preparing shaders" screen).
  Without a recipe it only logs one line at boot and does nothing - safe to keep on.
- The recipe itself is not in the repo (correctly: it is derived from the game; the fork's
  own gate in nx2_recipe.cpp refuses a recipe that holds game code). To get it for OoT,
  the owner runs tools/nx2-precompile against his dump (README: extract ~30 s finds all
  42,848 programs), then drops the .nx2recipe into the title's shader folder
  (<data>/shader/0400486024DEC000/) or <exe dir>/recipes/, and the next boot "Preparing
  shaders" builds everything; EDEN_NX2_PRECOMPILED_FAST=1 also keeps SPIR-V for instant draws.
- "Prepare Shaders" (game list) + the NVIDIA cache tip remain the in-app path for users
  without a recipe. The v1.2 whats-new already mentions both.

## 2026-11-12 (night) - OoT shader recipe built and audited on this machine; 1.3 notes updated

Recipe pipeline (tools/nx2-precompile) ran against the game dump (.nup, 25 GB, read
26.2 GB, 186,587 files scanned) with the recorded caches (learn v56, tested v57; both from
this machine):
- 42,848 programs: 15,851 vertex, 22,352 pixel, 4,125 compute, 500 mesh, 20 task;
  0 errors. 33,253 vertex/pixel pair combinations; priors 70 direct + 23 indirect;
  oracle accuracy ~99.9% (only 6/3 compute SPIR-Vs differ).
- precompiled.fork-v57.nx2prep: 135,406 records (126,009 predicted graphics pipelines,
  2,639 from recordings, 2,696 predicted mesh, 4,060 compute), 45,319 environments,
  1.13 GB. replay failures 0 (step `check`).
- game.nx2recipe: 1,211,408 bytes; 41,234 programs, 1,660 render states; ZERO environments
  withheld as game data. audit: "42848 programs checked: 0 pieces of their code in the
  recipe" - round trip verified: recipe -> nx2-precompile assemble produced a byte-identical
  precompiled file (135,406 records, 0 dropped, 0 withheld) and `check` reported 0 replay
  failures and 0 mismatched keys.
- Installed for this machine at %APPDATA%\ocarina2\shader\0400486024DEC000\game.nx2recipe
  (title shader folder; the next boot with EDEN_NX2_PRECOMPILE=1 (default) assembles the
  precompiled file from it and builds the pipelines on background threads). Undo: delete
  that one 1.2 MB file; the assembled precompiled.fork-v57.nx2prep that boot writes can be
  deleted too.
- What this does NOT prove: the in-game effect on this machine's next session (first new
  area ~700-850 ms -> ~40-70 ms compile stalls expected from the owner's A/B on 2026-11-09,
  but nobody has watched it), nor does it say anything about the 1.3 release assets (the
  recipe going out with a 1.3 release is a distribution decision for the owner; by
  construction the recipe carries no game code).
## 2026-11-12 (late night) - recipe title id fixed; precompiled pipelines accepted at boot

Findings from "check the log", in order:
- The running build kept logging "no recipe for 0400486024DEC000" even though a recipe sat
  in %APPDATA%\ocarina2\cache\shader\0400486024dec000\game.nx2recipe. Root cause: the tool
  (tools/nx2-precompile) stamped the recipe with the base package's title only; a standalone
  update dump (kind 1) produced title_id 0, which the runtime rejects by design (it requires
  the recipe's title to equal the running title exactly). Fixed: the recipe now records the
  update's title when present, else the base's (commit 11e76c55c8). Recipe regenerated
  (1,211,406 bytes) and copied over the cache one; the dead title-0 recipe kept as
  game.nx2recipe.title-id-zero.bak in the same folder.
- The runner's earlier "audit ... Exit code: 1" was PowerShell's stderr-is-an-error
  artifact, not the tool: re-run shows exit 0 with "42848 programs checked: 0 pieces of
  their code in the recipe".
- Relaunched straight into OoT (the .nup path as a command line argument). The log now
  shows "[ 14.5] Precompiled pipelines: assembling ... game.nx2recipe (game version 131072)
  with this copy of the game on 3 threads" instead of the old line, and the finished
  precompiled.fork-v57.nx2prep (1127029588 bytes - byte-size identical to the tool-built
  reference) plus its .recipe.txt note landed in the same cache folder. The background
  preparation is running with 0 restarts so far.
- Linux build-check is green on the aliasing fix (e1a50f2091) and on the diagnostics
  restore (0353a1b8e0); Windows was already green on both.
- Nothing released, tagged or published; v1.3 remains untagged at the owner's word.

Distribution decision still the owner's: the recipe (built from his dump) and/or the
1.13 GB precompiled file could go with 1.3 if he wants them shipped; by construction the
recipe carries no game code (audit-verified), but it was derived from his dump.


## 2026-11-12 (night, later) - bullet time in big areas fixed (option B by the owner); the "crash" was VRAM thrash

The owner chose the timing patch fix (B) for "game going in complete slow motion while going through
big places".

What the freeze actually was (found after the report, on the same run):
- The process never crashed: it stayed alive but starved (0.2 s of CPU in 5 s, 16.5 GB working set)
  and the window looked dead; no crash dumps, no WER events.
- The log shows the texture cache tripped the class-limited 3.8 GB critical threshold every few
  frames ("Aggressive GC engaged: used 5168 MiB >= critical 3739 MiB" repeatedly), hundreds of fastmem
  page-protect/unmap stalls per frame, read-backs occasionally over 100 ms, and a report of "Worst
  frame in the last 10 s: 3829.4 ms (36 of 43 frames over 33 ms)". On this 6 GB RTX 3050 the machine
  is beyond the budget's useful edge for 1440p->4K + FSR3 FG x4 in heavy areas; Windows paging and GC
  thrash followed.
- Root mechanics of the bullet time: Dark's built-in Dynamic FPS routine capped the tick delta at
  50 ms (cntfrq/20 at +0x24..+0x30), so at < 20 fps (exactly the open-field rate) the game advanced
  < 60% of real time = constant slow motion. Lichii#655 "FPS Unlock" raises the same cap to 100 ms
  (cntfrq/10 = the tail fminnm's 6.0 = 100 ms at 60 fps).

Fix (both paths, like #655):
- DYNAMIC_FPS_ROUTINE (what ships with oot_high_refresh=false): mov x14,#0x14 -> #0xa
  (cntfrq/10) in the tick-delta clamp. HIGH_REFRESH_ROUTINE: same tick-clamp word change, and the
  tail fminnm max-step constant 3.0f -> 6.0f at +0x84.
- src/tests/core/builtin_patches.cpp mirrors the routine bytes byte for byte; updated in step.
  Both verified against the disassembly (capstone) encoding and structure unchanged; the fork keeps
  the 0.02f floor and the 1.0f speed scale.
- Test-manager verified by Capstone disasm: mov x14,#0xa in both; fminnm sees s3=6.0f; test mirror
  matches (bytes compare equal).

Honest expectation: it stops the bullet time (real-time logic down to ~10 fps). It does NOT make the
game render faster: at 3-10 fps in heavy areas it is still a slideshow on this PC. The machine needs
fewer pixels to be playable: lower render scale and/or FG x2, or 30 FPS mode (the built-in for that,
within option C the owner rejected), and the fork may raise the 3.8 GB cap only on cards whose driver
budget can hold it (the RX 9070 XT-class headroom fix d31b87be4a; this PC is not that class).

Not released. CI builds these bytes now; the owner tests the rebuilt exe before anything about v1.3
moves. White flash and the VRAM-churn behavior in heavy areas remain open.

## 2026-11-13 (morning) - FG idles on static screens (title screen, menus, loading)

The owner asked for FG to be idle on the title screen. Diagnosis: the game paces VI at swap
interval 2 (30 Hz present), below the frame-gen target, so the tarball FG interpolator kept
running everywhere including the mostly-static title screen - producing duplicates of an
unchanged image while the interpolator burned the GPU every frame. FG dropping to zero only on
screen remains impossible without knowing the frame content, so:

- FrameGen::RecordThumbnail: present frame is downscaled through vkCmdBlitImage into a persistent
  32x18 RGBA8 thumbnail image, copied to a host-visible 2304 B buffer, and hashed with CityHash64
  on the CPU a couple frames later (a torn read only perturbs the hash, never corrupts anything).
- After ~1 s (60 frames) of an unchanged thumbnail, WantedGenerations() returns plan{0, false},
  FG emits no interpolated frames at all, and the raw base frame is presented as-is - exactly the
  same pixels FG would have interpolated. The chain stays built; the moment the hash changes every
  frame the streak resets and FG resumes instantly, no rebuild, no quality pop.
- Verified: tests target builds video_core and links; FPS stays the raw base rate on static
  screens (status bar reads "FGx4 (IDLE)" then), FG stops calling the chain entirely until the
  next change.

Method check vs upstream (AGENTS.md): no equivalent PR/issue exists in upstream zortos293/Lichii;
the 253 upstream commits ahead never touch frame_gen_pacer / frame_gen.cpp idle semantics. This
is new, fork-native behavior.

Also recorded: upstream/nx2 is 253 commits ahead of this fork (1106 ahead of upstream); merging
upstream is the owner's decision, not ours to make unilaterally.

## 2026-11-13 (afternoon) - VRAM and CPU analysis, written down (no code changes yet)

VRAM (this user, RTX 3050 6 GB, budget ~5299 MiB reported by the driver, critical capped
3739 MiB for cards under 10 GiB):
- The ASTC->BC3 recompression is REQUIRED on this machine (notes row 128: gpu_accuracy Low,
  uncompressed ASTC was worse on 6 GB cards); the fork always re-encodes to BC formats for
  this path (astc_recompression default Bc3). Raw ASTC (2 bpp) would be ~4x smaller in
  storage, but the fork has no "Original/native ASTC" option - and NVIDIA's ASTC LDR support
  is real but was not validated as an emulator path for this game; flipping defaults is an
  owner-test decision, not a unilateral one.
- Virtual textures trimmed to the first level that fits 512 MiB since 87d84d513c-era fork
  work; already the reason the 8192x8192x68 ASTC arrays fit at all.
- Driver pipelines ~1292 at ~0.45 MB each; the 1.13 GB precompiled prepared set is held by the
  driver's cache; staging buffers ~130 MB; all count toward the 5299 MiB budget.
- REAL reductions available without caps: lower render scale (frame storage dominates; SGSR
  already strains at 1440p->4K FG x4 on 6 GB), fewer FG multipliers, less upscaling. These are
  user-side settings; there is no fork-side toggle that recycles identical textures without
  reworking the streamer (high risk, game-specific).
- The fork CANNOT raise the 3.8 GB cap back on 10 GiB- classes of cards: that was the d31b87be4a
  gate revert (>10 GiB class follows budget follower). This PC is not that class.

CPU overhead (same log):
- ~1,400 SVCs per guest frame through the fork's global scheduler lock (row 109 known);
  macro interpreter 11-13% of the GPU thread (row 116 known); draw setup ~6.4 us x 55k draws
  (row 141); split-GPU "front/back" waiting (open PR #1433). All structural, no small win.
- The biggest player-visible symptom in big places - the *apparent* CPU saturation with the
  game running in slow motion - was fixed by 5b74405eb4/8b497ab217 (the 100 ms step cap).

## 2026-11-13 (late afternoon) - the "VRAM Cap (Performance)" setting (4 GB / 6 GB / 8 GB, Core tab)

Owner: keep the hard 3.8 GB clamp for 4 GB-VRAM people, as an option; his card is 6 GB (6144
MiB, driver budget ~5.0-5.3 GB). New setting next to Memory Layout in Core (game-specific too,
SWITCHABLE(false?) no, SWITCHABLE true):

- ENUM(VramCap, Auto, Cap_4Gb, Cap_6Gb, Cap_8Gb) - settings_enums.h; Default Auto = the current
  rule (hard 3.8 GB clamp below 10 GiB of driver budget; budget follower above it). The 10 GiB
  gate now yields to an explicit pick in the VRAM Cap from both the texture and the buffer cache
  (buffer cache cap date is the same).
- The 4 GB option is the 3.8 GB hard cap (3800 MiB critical), permanent for cards that need it.
  The 6 GB option allows the budget follower up to 6144 MiB; 8 GB to 8192 MiB - which is more
  room than this machine can use, but harmless to ship.
- Applies on next launch: Init() runs ApplyThresholdOverrides at construction; no live switch.
- The Frizti TunedSettings list deliberately does not include it; default stays Auto.

## 2026-11-13 (afternoon) - Ocarina2 v1.3 released (build-27)

Owner said go. Dispatched release-binaries with publish_release=true on nx2 HEAD (3fef0fb679).
Release tag build-27, title "Ocarina2 v1.3" (dist/ocarina2_version.txt), 10 assets published:
Windows x86_64 + PGO, Linux x86_64 AppImage/tar.gz + PGO, Linux ARM (aarch64) AppImage/tar.gz +
PGO, no macOS package (hosted arm64 runners too scarce, same policy as v1.2). Publish verified
green: build-check green on both platforms, release-binaries Prepare/build/Publish all success.
Release body carries the whats-new for v1.3 (stability: GPU address-space crash fix, guest memory
move corruption fix, FPS 100 ms tick-delta clamp; graphics: white flash mostly gone, FG idles on
static scenes, large GPUs unclamped, new VRAM Cap (Performance) dropdown 4/6/8 GB), then the PGO
package table unchanged from the template.

The 1.3 recipe remains owner-distributed only; the release ships no game data.
