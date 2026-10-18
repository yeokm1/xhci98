**Neither tree is safe as-is for a hot-removable NVMe endpoint.** nvme9x retains the critical reset, sector-size, and PRP-ownership defects. Its Win98 adaptations are useful, but the guard timer introduces a separate completion-liveness problem.

This was a static, read-only review. I made no changes and ran no builds, tests, or driver code. I excluded `SCSI.H` and `SRB.H`. Unless marked “upstream,” file links below refer to `nvme9x-src/x/src`.

For the previous 15 findings, several contain multiple defects, so their dispositions need qualification:

| # | Previous finding | Disposition in nvme9x, with evidence |
|---|---|---|
| **1** | Reset/abort releases requests without stopping hardware | **Present — critical.** [`nvme2k.c:1290`](nvme9x/src/nvme2k.c:1290) still completes outstanding requests without resetting hardware or clearing ownership. Abort and reset SRBs merely succeed at line 1123 onward. [`nvme2k_nvme.c:408`](nvme9x/src/nvme2k_nvme.c:408) still resolves completions by tag or `NonTaggedInFlight`, without generation validation. |
| **2** | Incorrect FLBAS decoding and malformed namespace structure | **Present — critical, unchanged in substance.** [`nvme2k_cpl.c:258`](nvme9x/src/nvme2k_cpl.c:258) computes `1 << (FLBAS & 15)`, substituting 512 for result 1. [`nvme.h:223`](nvme9x/src/nvme.h:223) places NGUID at 128 and LBAF at 152, despite comments claiming 104 and 128. |
| **3** | Flush frees another request’s PRP page; SMART frees uninitialized index | **Flush defect present; uninitialized SMART index fixed.** [`nvme2k_scsi.c:399`](nvme9x/src/nvme2k_scsi.c:399) does not initialize the flush SRB extension; [`nvme2k_cpl.c:443`](nvme9x/src/nvme2k_cpl.c:443) frees its page value. However, SMART initializes `prpPageIndex` from CID at [`nvme2k_cpl.c:13`](nvme9x/src/nvme2k_cpl.c:13). That fixes the uninitialized variable, not stale-completion ownership. |
| **4** | Overflowing transfer validation and zero-length commands | **Present in a stronger form.** The overflowing validation expression itself is absent because **there is no block-count-versus-buffer validation at all**. Parsing proceeds directly to command construction at [`nvme2k_nvme.c:465`](nvme9x/src/nvme2k_nvme.c:465). Line 603 still converts zero blocks to NVMe NLB=0, meaning one block, and permits oversized counts to spill into other CDW12 fields. |
| **5** | Unsafe raw NVMe passthrough buffers and oversized log transfers | **Not applicable to that interface.** The newer raw NVMe passthrough handler is absent. Dispatch at [`nvme2k.c:1144`](nvme9x/src/nvme2k.c:1144) only offers the older custom/SMART handlers. [`nvme2k_nvme.c:331`](nvme9x/src/nvme2k_nvme.c:331) fixes Get Log Page to 512 bytes. This is absence of the affected feature, not a general IOCTL-safety endorsement. |
| **6** | Unsafe initialization and failure cleanup | **Present; shutdown partially improved.** [`nvme2k.c:616`](nvme9x/src/nvme2k.c:616) clears admin registers before disabling the controller. Failures at lines 840 and 911 return without quiescing it. Shutdown now clears queue registers after the disable attempt, but [`nvme2k_nvme.c:864`](nvme9x/src/nvme2k_nvme.c:864) continues after that attempt times out. |
| **7** | Surprise removal lacks a terminal state; long waits | **Present, with different timer behaviour.** [`nvme2k_nvme.c:47`](nvme9x/src/nvme2k_nvme.c:47) treats all-ones CSTS as ready. [`nvme2k.c:1189`](nvme9x/src/nvme2k.c:1189) checks neither presence nor deadlines. Shutdown can still stall about 12 seconds; initialization’s disable retries about 30 seconds. The guard timer can instead stop polling prematurely, detailed below. |
| **8** | Advertised restart does not restore operation | **Present functionally, but with a different cause; adapter-control path is not applicable to 9x/NT4.** [`nvme2k.c:1342`](nvme9x/src/nvme2k.c:1342) calls a full `HwInitialize`, unlike newer upstream’s ineffective initialization stub. However, this initialization reallocates from an already-consumed pool and fails. See N2 below. |
| **9** | FUA and ORDERED-tag durability | **Present.** [`nvme2k_nvme.c:600`](nvme9x/src/nvme2k_nvme.c:600) never transfers SCSI FUA into NVMe CDW12. [`nvme2k_scsi.c:229`](nvme9x/src/nvme2k_scsi.c:229) queues a synthetic Flush immediately before the I/O without a completion barrier. Its completion has no SRB, so the decrement at `nvme2k_cpl.c:513` is skipped. |
| **10** | PCI configuration access from inappropriate callbacks; unchecked reads | **Present.** [`nvme2k.c:925`](nvme9x/src/nvme2k.c:925) accesses PCI configuration from `HwInitialize`, including restart. [`utils.c:748`](nvme9x/src/utils.c:748) and the following helpers ignore returned lengths. The documented NT SCSIport restriction permits reads only in FindAdapter or `ScsiSetRunningConfig`. [Microsoft contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/srb/nf-srb-scsiportgetbusdata). |
| **11** | Excessive uncached allocation and assumed alignment | **Ordinary allocation-size issue fixed; alignment and dump budget remain.** [`nvme2k.h:80`](nvme9x/src/nvme2k.h:80) requests 64 KiB, once, below the 100-KiB limit. It still exceeds the documented under-32-KiB dump/hibernation budget. [`utils.c:707`](nvme9x/src/utils.c:707) aligns only offsets, not the physical base-plus-offset. [Allocation contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/srb/nf-srb-scsiportgetuncachedextension). |
| **12** | Contradictory DMA flags and overriding port limits | **Flags fixed; port-limit defect present.** [`nvme2k.c:188`](nvme9x/src/nvme2k.c:188) selects 32-bit DMA for NT4/9x, otherwise clears that flag and enables 64-bit DMA. But lines 181 and 196 unconditionally assign 256 KiB and 511 breaks. The newer tree’s post-allocation transfer-limit increase is absent. |
| **13** | BAR high-half lost during fallback discovery | **Present, broader exposure.** [`nvme2k.c:220`](nvme9x/src/nvme2k.c:220) discards supplied resources and always reconstructs the address from the low BAR dword. There is no BAR1 handling at all. Sizing also occurs with memory decoding enabled. Unlike newer upstream, populated PnP resources do not avoid this path. |
| **14** | Invalid queue sizes and ring arithmetic | **Present.** [`nvme2k.c:579`](nvme9x/src/nvme2k.c:579) narrows MQES+1 to `USHORT`; 65536 becomes zero. Lines 690–770 use smaller non-power-of-two capacities with mask/log2 indexing. Page-size capability and doorbell-range validation are also absent. |
| **15** | INQUIRY EVPD misdecode and short-buffer overwrite | **EVPD defect present; specific overwrite not applicable.** [`nvme2k_scsi.c:63`](nvme9x/src/nvme2k_scsi.c:63) tests `PageCode` bit zero. This version has no supported-VPD-pages response, so the newer six-byte-check/eight-byte-write defect is absent. |

The following are additional issues or materially different failure mechanisms. “Additional” means relative to the supplied review; some are omissions in the older base, not changes demonstrably introduced by SweetLow.

1. **N1 — High: the guard timer abandons outstanding work.**  
   [`nvme2k.c:1194`](nvme9x/src/nvme2k.c:1194) polls after a quiet interval, then unconditionally clears `FallbackTimerActive`. It does not rearm based on either queue’s outstanding depth. The interval is 100 ms.

   **Scenario:** an operation takes 250 ms and its interrupt is lost. The timer polls at 100 ms, finds nothing and stops. With no subsequent submission, the completion remains unprocessed until external timeout/recovery. Activity elsewhere can also postpone polling because the activity flag is global.  
   **Fix:** keep a bounded watchdog active while either queue has outstanding commands; inspect deadlines and device presence regardless of unrelated activity.

2. **N2 — High: restart/reinitialization exhausts the allocation pool.**  
   [`nvme2k.c:700`](nvme9x/src/nvme2k.c:700) allocates all queues and the utility pool on every initialization. `UncachedExtensionOffset` is reset only in adapter discovery, at line 407.

   **Scenario:** after a successful start and stop, NT `ScsiRestartAdapter` calls `HwInitialize` again. With 64-entry queues, the first initialization consumes 53 KiB; the second reaches the 64-KiB limit before allocating its 40-KiB utility pool. This also affects a Win98 restart path if it reuses the extension.  
   **Fix:** separate allocation from hardware initialization and reuse allocations after established quiescence. Explicitly reset lifecycle flags, including the otherwise sticky `IsShutDown`.

3. **N3 — High, conditional on firmware use: Win98’s restore callback destroys rather than restores firmware state.**  
   [`nvme2k.c:1385`](nvme9x/src/nvme2k.c:1385) saves nothing; the restore branch shuts down the controller and returns success.

   **Scenario:** real-mode DOS/BIOS access expects the NVMe option ROM’s queue state after leaving protected-mode operation. It instead receives a disabled controller whose firmware state was never retained. Shutdown can be appropriate for device stop, but it does not implement firmware restoration.  
   **Fix:** distinguish stop from firmware handoff and implement a supported ownership transition; report failure when restoration cannot be performed. Microsoft defines this callback as saving and restoring HBA state. [Callback contract](https://learn.microsoft.com/en-us/previous-versions/windows/hardware/storage/ff557278%28v%3Dvs.85%29).

   Disabling `HwAdapterState` on NT alone is **not** evidence of a defect: that callback is not the general NT power-management mechanism.

4. **N4 — High: discovery can attach the wrong controller or use mismatched bus information.**  
   [`nvme2k.c:444`](nvme9x/src/nvme2k.c:444) computes `PNP` but never uses it to constrain discovery. Missing/non-NVMe devices fall through to scanning other slots. On success, lines 504–526 update `DevExt`, but not `ConfigInfo->SystemIoBusNumber/SlotNumber`.

   **Scenario:** an external controller disappears during discovery; the driver finds another NVMe device and configures it under the original instance. On manual discovery, configuration accesses and resource translation can refer to different buses.  
   **Fix:** probe only the supplied PnP function; synchronize configuration information on legitimate manual-scan paths. Newer upstream explicitly handles both cases at [`upstream nvme2k.c:506`](nvme2k-1.2.0.0/src/nvme2k.c:506).

5. **N5 — High: controller maximum transfer size is ignored.**  
   [`nvme.h:192`](nvme9x/src/nvme.h:192) hides MDTS inside reserved bytes; [`nvme2k.c:181`](nvme9x/src/nvme2k.c:181) advertises 256 KiB regardless.

   **Scenario:** a controller supports a maximum 128-KiB command, but the port supplies a permitted 256-KiB request. Ordinary large I/O fails despite valid buffers and LBAs.  
   **Fix:** decode MDTS using the controller’s minimum page size and enforce the minimum of hardware, port and PRP limits; split requests when necessary. Newer upstream has MDTS handling, although its arithmetic still needs review. [MDTS definition](https://learn.microsoft.com/en-us/windows/win32/api/nvme/ns-nvme-nvme_identify_controller_data).

6. **N6 — High on affected NT systems: legacy PCI interrupt-line data overwrites assigned routing.**  
   [`nvme2k.c:155`](nvme9x/src/nvme2k.c:155) overwrites interrupt level/vector on every target, or sets both to zero for an invalid interrupt-line byte.

   **Scenario:** Windows supplies valid PnP interrupt resources, but PCI configuration contains an unhelpful legacy line value. Initialization loses working interrupt routing, exposing the guard-timer defect.  
   **Fix:** retain port/HAL assignments on PnP targets; restrict manual routing to the platform path requiring it. Newer upstream does this at [`upstream nvme2k.c:214`](nvme2k-1.2.0.0/src/nvme2k.c:214).

7. **N7 — Medium: the Win98 cache-mode fallback changes PTEs without invalidating cached translations.**  
   [`utils.c:896`](nvme9x/src/utils.c:896) directly changes PTE cache bits, then returns. There is no TLB invalidation or cache-transition protocol.

   **Scenario:** `_PageModifyPermissions` fails, and an existing translation retains WB attributes while the PTE now requests UC. The workaround has not reliably established the memory attributes it promises. This is a demonstrated missing synchronization step, not proof that coherent WB DMA always fails.  
   **Fix:** use a supported VMM memory-type operation, or implement the required translation/cache synchronization and mapping checks. [Intel paging-cache guidance](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.pdf).

8. **N8 — Medium: unsupported SERVICE ACTION IN commands can remain pending forever.**  
   [`nvme2k.c:1059`](nvme9x/src/nvme2k.c:1059) handles READ CAPACITY(16), but otherwise breaks without assigning a status. The final completion block at line 1180 skips pending SRBs.

   **Scenario:** a diagnostic sends another service action with an initially pending SRB. No hardware command is submitted and no completion is generated.  
   **Fix:** validate CDB length and service action, and explicitly reject unsupported actions with appropriate sense data.

For **Win98 surprise removal**, three distinctions matter:

- **The guard timer itself contains no multi-second stall loop.** Its 100-ms value schedules a callback; it is not a 100-ms busy wait. The long stalls are in initialization and shutdown.
- With all-ones MMIO, normal shutdown can spend approximately **1 + 1 + 5 + 5 = 12 seconds** on queue deletion, shutdown status and ready-clear waits. Initialization can spend approximately **30 seconds** retrying disable. These loops are bounded, but violate the documented SCSIport stall-duration contract. [Stall restrictions](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/srb/nf-srb-scsiportstallexecution).
- Timeout recovery remains unsafe: reset completes requests without establishing hardware quiescence or invalidating their mappings. A closed tunnel gate can stop current DMA, but it cannot repair the miniport’s stale request bookkeeping or make reopening the tunnel safe.

SweetLow’s shutdown work is worth retaining: [`nvme2k_nvme.c:728`](nvme9x/src/nvme2k_nvme.c:728) explains and implements disabling timer rearming before shutdown commands, and normally cancels the timer. However, `FallbackTimer` itself checks neither `FallbackTimerNeeded` nor lifecycle state; cancellation is skipped when `IsShutDown` is true. The complete repair needs explicit running/stopping/gone states, admission gating, bounded recovery, and command-generation tracking.

For **4-KiB sectors**, the current behaviour is unequivocally unsafe:

| Actual namespace format | Driver-reported block size |
|---|---:|
| Format index 0, LBADS=9 | 512 — accidentally correct |
| Format index 0, LBADS=12 | 512 — wrong |
| Format index 1, LBADS=12 | 2 — wrong |

FLBAS selects a format; the selected LBAF’s LBADS supplies the exponent. LBAF begins at byte 128. [Namespace layout](https://learn.microsoft.com/en-us/windows/win32/api/nvme/ns-nvme-nvme_identify_namespace_data), [LBA format encoding](https://learn.microsoft.com/en-us/windows/win32/api/nvme/ns-nvme-nvme_lba_format).

A one-block read into a page-aligned 512-byte buffer can therefore cause a **4096-byte device DMA write**, overwriting adjacent memory. Fixing only the FLBAS expression is insufficient because the structure’s LBAF offset is wrong too. Correct both, validate the selected format and metadata/protection settings, and check transfer sizes before building PRPs.

The **64-bit LBA conversion itself looks sound**: [`nvme2k_nvme.c:490`](nvme9x/src/nvme2k_nvme.c:490), [`utils.c:857`](nvme9x/src/utils.c:857), and [`nvme2k_scsi.c:182`](nvme9x/src/nvme2k_scsi.c:182) preserve the upper LBA bits and encode READ CAPACITY(16) correctly for the normal case. That does not establish large-disk support throughout Win98’s upper storage/filesystem stack. Likewise, correctly reporting 4096-byte sectors would require separate upper-stack validation; silently presenting them as 512-byte sectors is not sector translation.

**An AMD64 extension is technically reasonable, but this tree is not AMD64-ready.** The main work would be:

- Replace or extend [`atomic.h:19`](nvme9x/src/atomic.h:19): it supports x86 assembly and Alpha, then explicitly errors on other architectures. The guard timer actively needs an exchange operation returning the previous value. Use suitable compiler intrinsics; MSVC x64 does not support inline assembly. [Microsoft guidance](https://learn.microsoft.com/en-us/cpp/intrinsics/compiler-intrinsics).
- Keep all VMM calls, direct PTE manipulation and 32-bit pointer casts confined to the Win9x build. Use the NT mapping path on AMD64.
- Use AMD64 DDK headers and ABI definitions, keeping the NT4/9x 76-byte initialization-structure assertion out of that build. Reconcile compatibility definitions in `scsi64.h` with the chosen DDK.
- Preserve 64-bit assigned BAR addresses and DMA addresses. The existing PRP and queue-address fields already use `ULONGLONG`/`PHYSICAL_ADDRESS`; a wholesale DMA-model rewrite is unnecessary.
- Add AMD64 build, INF, version/resource and installation/signing support. Newer upstream already provides an explicit AMD64 target in [`WDM/sources:20`](nvme2k-1.2.0.0/src/WDM/sources:20), plus the corresponding INF.

The main engineering burden remains safe ownership and recovery across all targets, rather than pointer width alone.

On **provenance**, [`readme.txt:3`](nvme9x/readme.txt:3) identifies nvme2k 1.0.0.2 and describes the modifications. [`nvme2k.rc:15`](nvme9x/src/nvme2k.rc:15) credits Dominik Behr and SweetLow. However, the reviewed `x` tree has **no LICENSE/NOTICE file**, and the non-DDK C/H files have no copyright/licence headers or SPDX identifiers.

The newer upstream snapshot includes a [BSD-3-Clause licence](nvme2k-1.2.0.0/src/LICENSE). That establishes the notice supplied with that snapshot; it does not by itself document the licence of SweetLow’s additions. Before incorporating them, establish their licence and exact source provenance, preserve the applicable notices, and keep the Microsoft DDK headers separate. Their contents were not reviewed.

**Verdict: nvme9x is the worse primary base for a fork spanning Win98/2000/XP x86 and XP/7 x64.** Use upstream nvme2k 1.2.0.0 as the integration base, repair its critical shared defects, and selectively port the Win98 ABI, mapping and shutdown adaptations from nvme9x after resolving provenance. Rework the guard timer and state transitions instead of copying them unchanged. nvme9x is a valuable Win98 reference, but neither branch currently provides safe hot-removal or 4-KiB-sector support.

