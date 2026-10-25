/*
 * cm_ejask.h - the Windows 98 SE and ME eject asked of xhcicm.sys (design
 * record 21 section 7.2, "The 9x half", part 1; record 24 section 7.1,
 * revision 6; roadmap-usb4.md task 43.18): the three registry values the
 * install tool, the USB tree and the connection manager exchange, their
 * layouts and their codes. Shared byte for byte by the driver
 * (cm_eject.c, cm_ejectos.c), XHCIINST.EXE and XHCITR.EXE
 * (xhciinst\inst_ej9x.c) and the host tests, so it uses no DDK or Windows
 * type.
 *
 * Every field is a little-endian 32-bit unsigned value at a fixed offset;
 * the readers take bytes, never a structure overlay. A location is a bus,
 * device and function as the status record's port words hold one (bus
 * 15:8, device 7:3, function 2:0).
 *
 * THE ASK. CM_EJ_ASK_VALUE, REG_BINARY of CM_EJ_ASK_BYTES, in the NHI's
 * driver key beside XhciCmStatusAsk, written by the tool under the shared
 * eject mutex (XhciinstTbAsk) and deleted by it at the end of its run. The
 * connection manager only reads it (at most every 500 ms, with the status
 * ask), takes a nonce it has not seen, and never takes the ask it finds
 * at its start.
 *
 * THE REPLY. CM_EJ_REPLY_VALUE, REG_BINARY of CM_EJ_REPLY_BYTES, in the
 * NHI's hardware key beside XhciCmStatus: a rejection, made before any
 * call, with its reason. Nothing was changed.
 *
 * THE RESULT. CM_EJ_RESULT_VALUE, REG_BINARY of CM_EJ_RESULT_BYTES, in the
 * same key: an accepted ask's transaction, written at its acceptance (the
 * acceptance word, never changed afterwards) and at every step and every
 * retry after it (the clock field moves). The slot belongs to one
 * accepted transaction until the tool has consumed its result (its ask
 * deleted after a terminal step, or CM_EJ_CONSUME_MS after the terminal
 * step was written); a rejection never writes it.
 */

#ifndef CM_EJASK_H
#define CM_EJASK_H

#define CM_EJ_ASK_VALUE         "XhciCmEjectAsk"
#define CM_EJ_REPLY_VALUE       "XhciCmEjectReply"
#define CM_EJ_RESULT_VALUE      "XhciCmEjectResult"

/* The ask. */
#define CM_EJ_ASK_BYTES         28UL
#define CM_EJ_A_NONCE           0UL     /* nonzero, differs from the last  */
#define CM_EJ_A_MEMBER          4UL     /* the tunnel port's location      */
#define CM_EJ_A_UNIT            8UL     /* the unit's location; bus 0: the
                                           member's secondary bus (a tool on
                                           98 SE and ME reads no bus number:
                                           its instance id names the devfn
                                           below the member)               */
#define CM_EJ_A_EPOCH_LO        12UL    /* the query device's start epoch  */
#define CM_EJ_A_EPOCH_HI        16UL    /* (CM_Q_O_EPOCH_*) and content    */
#define CM_EJ_A_CONTENT_GEN     20UL    /* generation the unit was chosen
                                           under                           */
#define CM_EJ_A_FLAGS           24UL    /* 0                               */

/* The reply. */
#define CM_EJ_REPLY_BYTES       8UL
#define CM_EJ_R_NONCE           0UL
#define CM_EJ_R_REASON          4UL     /* CM_EJ_WHY_*                     */

#define CM_EJ_WHY_EPOCH         1UL     /* not this start's epoch          */
#define CM_EJ_WHY_GENERATION    2UL     /* not the current content
                                           generation                      */
#define CM_EJ_WHY_MEMBER        3UL     /* not an exclusive, active member
                                           of this controller's set        */
#define CM_EJ_WHY_NOTHING       4UL     /* nothing answers at the unit's
                                           location                        */
#define CM_EJ_WHY_NOT_BELOW     5UL     /* the unit's bus is not that
                                           member's secondary bus          */
#define CM_EJ_WHY_BUSY          6UL     /* a transaction runs on the
                                           member, or a result is not yet
                                           consumed                        */
#define CM_EJ_WHY_BAD           7UL     /* the ask does not check          */
#define CM_EJ_WHY_UNAVAILABLE   8UL     /* no call worker, the host
                                           interface lost, or the domain
                                           not running                     */
#define CM_EJ_WHY_NO_CHANNEL    9UL     /* an NVMe function no Stor98
                                           channel entry names (no Stor98
                                           running it, or none has read the
                                           channel): it cannot be asked to
                                           stop, and a removal alone is no
                                           isolation (stor98cm.h R2)       */

/* The result. */
#define CM_EJ_RESULT_BYTES      32UL
#define CM_EJ_S_NONCE           0UL     /* the ask's; 0: an empty slot     */
#define CM_EJ_S_ACCEPTED        4UL     /* CM_EJ_ACCEPTED, written once    */
#define CM_EJ_S_STEP            8UL     /* CM_EJ_STEP_*                    */
#define CM_EJ_S_QUERY_CR        12UL    /* Query_Remove_SubTree's CONFIGRET,
                                           CM_EJ_NO_CR before it returned  */
#define CM_EJ_S_REMOVE_CR       16UL    /* the last Remove_SubTree's, the
                                           same                            */
#define CM_EJ_S_ANSWER          20UL    /* the request's sequence 31:8 and
                                           Stor98's RemoveAck outcome 7:0
                                           (FEh none, FFh late); 0 for no
                                           request                         */
#define CM_EJ_S_OUTCOME         24UL    /* CM_EJ_OUT_*                     */
#define CM_EJ_S_CLOCK_MS        28UL    /* the connection manager's clock at
                                           the write                       */

#define CM_EJ_ACCEPTED          0x54504341UL    /* 'ACPT' */
#define CM_EJ_NO_CR             0xFFFFFFFFUL

#define CM_EJ_STEP_ACCEPTED     1UL
#define CM_EJ_STEP_QUERIED      2UL     /* Query_Remove_SubTree accepted   */
#define CM_EJ_STEP_REFUSED      3UL     /* ... refused: terminal           */
#define CM_EJ_STEP_REQUESTED    4UL     /* REMOVE_PENDING made             */
#define CM_EJ_STEP_ANSWERED     5UL     /* its answer, or its end, taken   */
#define CM_EJ_STEP_ISOLATING    6UL     /* the tunnel's teardown and the
                                           statement's wait                */
#define CM_EJ_STEP_REMOVING     7UL     /* Remove_SubTree, its retries     */
#define CM_EJ_STEP_REMOVED      8UL     /* terminal                        */
#define CM_EJ_STEP_BLOCKED      9UL     /* terminal                        */

/* What a terminal step came to, and on the way there the branch taken. */
#define CM_EJ_OUT_NONE          0UL
#define CM_EJ_OUT_PLAIN         1UL     /* removed, no request (not an
                                           NVMe function with an UP entry) */
#define CM_EJ_OUT_CLEAN         2UL     /* removed after CLEAN             */
#define CM_EJ_OUT_NOT_CLEAN     3UL     /* removed after ISOLATED_NOT_CLEAN:
                                           the drive returns as a new
                                           device                          */
#define CM_EJ_OUT_ISOLATED      4UL     /* removed after an isolation this
                                           driver made and proved          */
#define CM_EJ_OUT_GONE          5UL     /* the unit left the bus on the way
                                           (removed by its unplug)         */
#define CM_EJ_OUT_NO_STOP       6UL     /* blocked: the drive did not stop
                                           and no isolation could be made  */
#define CM_EJ_OUT_KEEPS_FAILING 7UL     /* blocked: Remove_SubTree kept
                                           failing (R11 O6)                */
#define CM_EJ_OUT_LOST          8UL     /* blocked: the devnode could not
                                           be found or named again         */

/* Stor98's answer in CM_EJ_S_ANSWER's low byte (stor98cm.h R9), and the
 * wait's own ends. */
#define CM_EJ_ANS_CLEAN         1UL
#define CM_EJ_ANS_ISOLATED      2UL
#define CM_EJ_ANS_FAILED        3UL
#define CM_EJ_ANS_NONE          0xFEUL
#define CM_EJ_ANS_LATE          0xFFUL

/* The tool's clocks (record 21 section 7.2 part 1, "The result"). */
#define CM_EJ_POLL_MS           250UL   /* the tool's poll                 */
#define CM_EJ_ANSWER_MS         3000UL  /* no reply and no result: the ask
                                           deleted ...                     */
#define CM_EJ_AFTER_MS          3000UL  /* ... and this much more waited   */
#define CM_EJ_BUDGET_MS         50000UL /* from acceptance to a terminal
                                           step: Stor98's 14 s, O6's 30 s
                                           and a 5 s margin, rounded up    */
#define CM_EJ_PROGRESS_MS       20000UL /* a result whose clock stood still
                                           this long                       */
#define CM_EJ_RESTAMP_MS        5000UL  /* the driver re-stamps a pending
                                           wait at least this often        */
#define CM_EJ_CONSUME_MS        60000UL /* a terminal result unconsumed
                                           this long frees the slot        */

#endif /* CM_EJASK_H */
