/*
 * hub_port_vectors.h - one downstream port of an external USB 2.0 hub, as the
 * bus's own hub class will track it (roadmap tasks 27-A.1 and 27-A.4), written
 * down as data before the code that runs it exists.
 *
 * This file is a **vector table and nothing else**. No function in src\ runs
 * it yet; test\test_topo.c checks only that the table is well formed (every
 * change bit cleared, the enumeration lock never leaked, a speed decoded on
 * exactly the rows that decode one, no two rows for one input). The hub-class
 * code of 27-A.1 is the consumer, and the interface it is written against is
 * an assumption made here, not a decision taken anywhere else:
 *
 *   ULONG XhciHubPortStep(ULONG state,        HPV_ST_*
 *                         ULONG event,        HPV_EV_*
 *                         ULONG attempt,      1..3, or 0 where none applies
 *                         ULONG portStatus,   wPortStatus of a GET_STATUS reply
 *                         ULONG portChange,   wPortChange of the same reply
 *                         PULONG actions,     out: HPV_ACT_* bits
 *                         PULONG speed);      out: XHCI_SPEED_* this step decoded
 *
 * returning the next HPV_ST_*. A pure transition function in the
 * src\xhci_enum.c sense: no I/O, no timer, no lock - the caller issues the
 * actions, owns the timers and the enumeration lock, and keeps the speed and
 * the attempt count. If 27-A.1 chooses another shape, the rows still say what
 * each input must produce; only the adapter changes.
 *
 * SOURCES. The steps are design record 13 section 10.2 ("An external hub's
 * port"), its suspend paragraph, and section 10.5's removal triggers; the
 * disconnect and swap rows are design record 02's graph table ("disconnects").
 * The wPortStatus and wPortChange bits are the XHCI_HUB_* values in
 * src\xhci.h (usb200.h 31-38; design record 13 section 10.1 marks the change
 * bits 1 to 4 "to transcribe, USB 2.0 Table 11-22"). The state names map onto
 * USB 2.0 section 11.5's downstream facing port states where the comment says
 * so; that specification is not in docs\references, so the mapping is **to
 * transcribe** like every other USB 2.0 number design record 13 cites.
 *
 * NOT COVERED, deliberately: over-current (design record 13 section 10 sets
 * no policy for C_PORT_OVER_CURRENT, so no row may invent one), port power
 * switching after configuration, and the timings themselves (TATTDB, TDRST,
 * TRSTRCY, TRSMRCY, bPwrOn2PwrGood) - a row names which timer is started, not
 * how long it runs.
 *
 * Include src\xhci.h first: ULONG, XHCI_HUB_* and XHCI_SPEED_* come from it.
 *
 * C89, data only.
 */

#ifndef XHCI_HUB_PORT_VECTORS_H
#define XHCI_HUB_PORT_VECTORS_H

/* ------------------------------------------------------------------ */
/* States                                                              */
/* ------------------------------------------------------------------ */

#define HPV_ST_POWER_OFF        0   /* 11.5 Powered-off (to transcribe)     */
#define HPV_ST_POWERING         1   /* PORT_POWER sent, bPwrOn2PwrGood x 2 ms */
#define HPV_ST_DISCONNECTED     2   /* 11.5 Disconnected                    */
#define HPV_ST_DEBOUNCE         3   /* connect seen, TATTDB running         */
#define HPV_ST_RESETTING        4   /* 11.5 Resetting: PORT_RESET sent      */
#define HPV_ST_RECOVERY         5   /* TRSTRCY after the reset completed    */
#define HPV_ST_ADDRESSING       6   /* Enable Slot / Address Device in flight */
#define HPV_ST_ENABLED          7   /* 11.5 Enabled                         */
#define HPV_ST_SUSPENDED        8   /* 11.5 Suspended                       */
#define HPV_ST_RESUMING         9   /* 11.5 Resuming: CLEAR PORT_SUSPEND sent */
#define HPV_ST_RESUME_RECOVERY  10  /* TRSMRCY after C_PORT_SUSPEND         */
#define HPV_ST_DISABLED         11  /* 11.5 Disabled: given up until the next
                                     * connect change (10.2 step 7)          */
#define HPV_ST_COUNT            12

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

#define HPV_EV_HUB_CONFIGURED   1   /* 10.3 steps 1-3 done; power the ports  */
#define HPV_EV_TIMER            2   /* the timer the last row started expired */
#define HPV_EV_STATUS           3   /* GET_STATUS(port) completed            */
#define HPV_EV_RESET_TIMEOUT    4   /* no C_PORT_RESET in 500 ms (bus policy) */
#define HPV_EV_ADDRESS_OK       5   /* Address Device and the device descriptor */
#define HPV_EV_ADDRESS_FAIL     6
#define HPV_EV_ADDRESS_TT_ERROR 7   /* Address Device: USB Transaction Error,
                                     * device behind a TT (xHCI p.102)      */
#define HPV_EV_SUSPEND          8   /* the bus decided to suspend the port   */
#define HPV_EV_RESUME           9   /* ...and to resume it                   */
#define HPV_EV_COUNT            10

/* ------------------------------------------------------------------ */
/* Actions (bits; the caller issues them in this order, low bit first) */
/* ------------------------------------------------------------------ */

#define HPV_ACT_SET_POWER           0x00000001UL /* SET_FEATURE(PORT_POWER)   */
#define HPV_ACT_GET_STATUS          0x00000002UL
#define HPV_ACT_CLEAR_C_CONNECTION  0x00000004UL
#define HPV_ACT_CLEAR_C_ENABLE      0x00000008UL
#define HPV_ACT_CLEAR_C_SUSPEND     0x00000010UL
#define HPV_ACT_CLEAR_C_OVER_CURRENT 0x00000020UL
#define HPV_ACT_CLEAR_C_RESET       0x00000040UL
#define HPV_ACT_CLEAR_TT_BUFFER     0x00000080UL /* to the TT hub, 10.2 step 7 */
#define HPV_ACT_SET_RESET           0x00000100UL /* SET_FEATURE(PORT_RESET)   */
#define HPV_ACT_DISABLE             0x00000200UL /* CLEAR_FEATURE(PORT_ENABLE) */
#define HPV_ACT_SET_SUSPEND         0x00000400UL
#define HPV_ACT_CLEAR_SUSPEND       0x00000800UL
#define HPV_ACT_ADDRESS             0x00001000UL /* Enable Slot + Address Device
                                                  * at the speed kept from the
                                                  * reset row                  */
#define HPV_ACT_ENUM_LOCK           0x00002000UL /* 10.2 step 3: one device
                                                  * between port reset and
                                                  * Address Device completion  */
#define HPV_ACT_ENUM_UNLOCK         0x00004000UL
#define HPV_ACT_REPORT_GONE         0x00008000UL /* 10.5: the device on this
                                                  * port and its subtree       */
#define HPV_ACT_TIMER_POWER_GOOD    0x00010000UL
#define HPV_ACT_TIMER_DEBOUNCE      0x00020000UL
#define HPV_ACT_TIMER_RESET         0x00040000UL /* the 500 ms reset time-out  */
#define HPV_ACT_TIMER_RECOVERY      0x00080000UL /* TRSTRCY or TRSMRCY         */

/* Shorthands for the rows below only. */
#define HPV_P   XHCI_HUB_PORT_POWER
#define HPV_C   XHCI_HUB_PORT_CONNECTION
#define HPV_E   XHCI_HUB_PORT_ENABLE
#define HPV_S   XHCI_HUB_PORT_SUSPEND

typedef struct _HPV_ROW {
    const char *What;
    ULONG State;
    ULONG Event;
    ULONG Attempt;      /* 10.2 step 7: three attempts; 0 where none applies */
    ULONG PortStatus;   /* 0 for every event but HPV_EV_STATUS              */
    ULONG PortChange;
    ULONG NextState;
    ULONG Actions;
    ULONG Speed;        /* XHCI_SPEED_* this row decodes, else UNKNOWN      */
} HPV_ROW;

static const HPV_ROW hpvRows[] = {
    /* Bring-up: 10.2 hub step 1, 10.3 step 4. */
    { "configured hub powers the port",
      HPV_ST_POWER_OFF, HPV_EV_HUB_CONFIGURED, 0, 0, 0,
      HPV_ST_POWERING, HPV_ACT_SET_POWER | HPV_ACT_TIMER_POWER_GOOD,
      XHCI_SPEED_UNKNOWN },
    { "power good: read the port once, do not assume (10.3 step 4)",
      HPV_ST_POWERING, HPV_EV_TIMER, 0, 0, 0,
      HPV_ST_DISCONNECTED, HPV_ACT_GET_STATUS, XHCI_SPEED_UNKNOWN },

    /* Connect: 10.2 hub step 2. */
    { "empty port, nothing changed",
      HPV_ST_DISCONNECTED, HPV_EV_STATUS, 0, HPV_P, 0,
      HPV_ST_DISCONNECTED, 0, XHCI_SPEED_UNKNOWN },
    { "connect change: clear it and debounce",
      HPV_ST_DISCONNECTED, HPV_EV_STATUS, 0, HPV_P | HPV_C,
      XHCI_HUB_C_PORT_CONNECTION,
      HPV_ST_DEBOUNCE, HPV_ACT_CLEAR_C_CONNECTION | HPV_ACT_TIMER_DEBOUNCE,
      XHCI_SPEED_UNKNOWN },
    { "device present at power-on with no change bit (10.3 step 4)",
      HPV_ST_DISCONNECTED, HPV_EV_STATUS, 0, HPV_P | HPV_C, 0,
      HPV_ST_DEBOUNCE, HPV_ACT_TIMER_DEBOUNCE, XHCI_SPEED_UNKNOWN },
    { "a connect that came and went before the read",
      HPV_ST_DISCONNECTED, HPV_EV_STATUS, 0, HPV_P,
      XHCI_HUB_C_PORT_CONNECTION,
      HPV_ST_DISCONNECTED, HPV_ACT_CLEAR_C_CONNECTION, XHCI_SPEED_UNKNOWN },
    { "debounce timer: re-read until stable",
      HPV_ST_DEBOUNCE, HPV_EV_TIMER, 0, 0, 0,
      HPV_ST_DEBOUNCE, HPV_ACT_GET_STATUS, XHCI_SPEED_UNKNOWN },
    { "stable connect: take the lock and reset (10.2 steps 3-4)",
      HPV_ST_DEBOUNCE, HPV_EV_STATUS, 0, HPV_P | HPV_C, 0,
      HPV_ST_RESETTING,
      HPV_ACT_ENUM_LOCK | HPV_ACT_SET_RESET | HPV_ACT_TIMER_RESET,
      XHCI_SPEED_UNKNOWN },
    { "bounced during debounce: start it again",
      HPV_ST_DEBOUNCE, HPV_EV_STATUS, 0, HPV_P | HPV_C,
      XHCI_HUB_C_PORT_CONNECTION,
      HPV_ST_DEBOUNCE, HPV_ACT_CLEAR_C_CONNECTION | HPV_ACT_TIMER_DEBOUNCE,
      XHCI_SPEED_UNKNOWN },
    { "gone during debounce: abandon",
      HPV_ST_DEBOUNCE, HPV_EV_STATUS, 0, HPV_P, XHCI_HUB_C_PORT_CONNECTION,
      HPV_ST_DISCONNECTED, HPV_ACT_CLEAR_C_CONNECTION, XHCI_SPEED_UNKNOWN },

    /* Reset: 10.2 hub steps 4-5; speed from bits 9 and 10, neither is FS. */
    { "reset done, High Speed",
      HPV_ST_RESETTING, HPV_EV_STATUS, 1,
      HPV_P | HPV_C | HPV_E | XHCI_HUB_PORT_HIGH_SPEED,
      XHCI_HUB_C_PORT_RESET,
      HPV_ST_RECOVERY, HPV_ACT_CLEAR_C_RESET | HPV_ACT_TIMER_RECOVERY,
      XHCI_SPEED_HIGH },
    { "reset done, Low Speed",
      HPV_ST_RESETTING, HPV_EV_STATUS, 1,
      HPV_P | HPV_C | HPV_E | XHCI_HUB_PORT_LOW_SPEED,
      XHCI_HUB_C_PORT_RESET,
      HPV_ST_RECOVERY, HPV_ACT_CLEAR_C_RESET | HPV_ACT_TIMER_RECOVERY,
      XHCI_SPEED_LOW },
    { "reset done, neither speed bit: Full Speed",
      HPV_ST_RESETTING, HPV_EV_STATUS, 1, HPV_P | HPV_C | HPV_E,
      XHCI_HUB_C_PORT_RESET,
      HPV_ST_RECOVERY, HPV_ACT_CLEAR_C_RESET | HPV_ACT_TIMER_RECOVERY,
      XHCI_SPEED_FULL },
    { "reset done but not enabled, attempt 1: reset again (step 5, 7)",
      HPV_ST_RESETTING, HPV_EV_STATUS, 1, HPV_P | HPV_C,
      XHCI_HUB_C_PORT_RESET,
      HPV_ST_RESETTING,
      HPV_ACT_CLEAR_C_RESET | HPV_ACT_SET_RESET | HPV_ACT_TIMER_RESET,
      XHCI_SPEED_UNKNOWN },
    { "reset done but not enabled, attempt 3: give the port up",
      HPV_ST_RESETTING, HPV_EV_STATUS, 3, HPV_P | HPV_C,
      XHCI_HUB_C_PORT_RESET,
      HPV_ST_DISABLED,
      HPV_ACT_CLEAR_C_RESET | HPV_ACT_DISABLE | HPV_ACT_ENUM_UNLOCK,
      XHCI_SPEED_UNKNOWN },
    { "no C_PORT_RESET in time, attempt 1",
      HPV_ST_RESETTING, HPV_EV_RESET_TIMEOUT, 1, 0, 0,
      HPV_ST_RESETTING, HPV_ACT_SET_RESET | HPV_ACT_TIMER_RESET,
      XHCI_SPEED_UNKNOWN },
    { "no C_PORT_RESET in time, attempt 3",
      HPV_ST_RESETTING, HPV_EV_RESET_TIMEOUT, 3, 0, 0,
      HPV_ST_DISABLED, HPV_ACT_DISABLE | HPV_ACT_ENUM_UNLOCK,
      XHCI_SPEED_UNKNOWN },
    { "unplugged mid-reset: lock released (10.5 step 1)",
      HPV_ST_RESETTING, HPV_EV_STATUS, 1, HPV_P, XHCI_HUB_C_PORT_CONNECTION,
      HPV_ST_DISCONNECTED,
      HPV_ACT_CLEAR_C_CONNECTION | HPV_ACT_ENUM_UNLOCK, XHCI_SPEED_UNKNOWN },

    /* Recovery and addressing: 10.2 hub steps 6-7. */
    { "reset recovery over: address the device",
      HPV_ST_RECOVERY, HPV_EV_TIMER, 1, 0, 0,
      HPV_ST_ADDRESSING, HPV_ACT_ADDRESS, XHCI_SPEED_UNKNOWN },
    { "unplugged during recovery",
      HPV_ST_RECOVERY, HPV_EV_STATUS, 1, HPV_P, XHCI_HUB_C_PORT_CONNECTION,
      HPV_ST_DISCONNECTED,
      HPV_ACT_CLEAR_C_CONNECTION | HPV_ACT_ENUM_UNLOCK, XHCI_SPEED_UNKNOWN },
    { "addressed: release the lock",
      HPV_ST_ADDRESSING, HPV_EV_ADDRESS_OK, 1, 0, 0,
      HPV_ST_ENABLED, HPV_ACT_ENUM_UNLOCK, XHCI_SPEED_UNKNOWN },
    { "address failed, attempt 1: reset again",
      HPV_ST_ADDRESSING, HPV_EV_ADDRESS_FAIL, 1, 0, 0,
      HPV_ST_RESETTING, HPV_ACT_SET_RESET | HPV_ACT_TIMER_RESET,
      XHCI_SPEED_UNKNOWN },
    { "transaction error behind a TT: CLEAR_TT_BUFFER first (xHCI p.102)",
      HPV_ST_ADDRESSING, HPV_EV_ADDRESS_TT_ERROR, 1, 0, 0,
      HPV_ST_RESETTING,
      HPV_ACT_CLEAR_TT_BUFFER | HPV_ACT_SET_RESET | HPV_ACT_TIMER_RESET,
      XHCI_SPEED_UNKNOWN },
    { "address failed, attempt 3: give the port up",
      HPV_ST_ADDRESSING, HPV_EV_ADDRESS_FAIL, 3, 0, 0,
      HPV_ST_DISABLED, HPV_ACT_DISABLE | HPV_ACT_ENUM_UNLOCK,
      XHCI_SPEED_UNKNOWN },
    { "unplugged while addressing: the half-made slot is the caller's",
      HPV_ST_ADDRESSING, HPV_EV_STATUS, 1, HPV_P, XHCI_HUB_C_PORT_CONNECTION,
      HPV_ST_DISCONNECTED,
      HPV_ACT_CLEAR_C_CONNECTION | HPV_ACT_ENUM_UNLOCK, XHCI_SPEED_UNKNOWN },

    /* Enabled: the removal triggers of 10.5 and design record 02's rows. */
    { "a poll with nothing changed",
      HPV_ST_ENABLED, HPV_EV_STATUS, 0, HPV_P | HPV_C | HPV_E, 0,
      HPV_ST_ENABLED, 0, XHCI_SPEED_UNKNOWN },
    { "disconnect: report the device and its subtree gone",
      HPV_ST_ENABLED, HPV_EV_STATUS, 0, HPV_P, XHCI_HUB_C_PORT_CONNECTION,
      HPV_ST_DISCONNECTED,
      HPV_ACT_CLEAR_C_CONNECTION | HPV_ACT_REPORT_GONE, XHCI_SPEED_UNKNOWN },
    { "swap between polls: connected, change bit set (DR02 B10)",
      HPV_ST_ENABLED, HPV_EV_STATUS, 0, HPV_P | HPV_C | HPV_E,
      XHCI_HUB_C_PORT_CONNECTION,
      HPV_ST_DEBOUNCE,
      HPV_ACT_CLEAR_C_CONNECTION | HPV_ACT_REPORT_GONE |
      HPV_ACT_TIMER_DEBOUNCE, XHCI_SPEED_UNKNOWN },
    { "the hub disabled the port: C_PORT_ENABLE with enable 0 (10.5)",
      HPV_ST_ENABLED, HPV_EV_STATUS, 0, HPV_P | HPV_C,
      XHCI_HUB_C_PORT_ENABLE,
      HPV_ST_DISABLED,
      HPV_ACT_CLEAR_C_ENABLE | HPV_ACT_REPORT_GONE, XHCI_SPEED_UNKNOWN },
    { "two changes in one read: each cleared (10.1)",
      HPV_ST_ENABLED, HPV_EV_STATUS, 0, HPV_P,
      XHCI_HUB_C_PORT_CONNECTION | XHCI_HUB_C_PORT_ENABLE,
      HPV_ST_DISCONNECTED,
      HPV_ACT_CLEAR_C_CONNECTION | HPV_ACT_CLEAR_C_ENABLE |
      HPV_ACT_REPORT_GONE, XHCI_SPEED_UNKNOWN },

    /* Suspend and resume: 10.2's suspend paragraph. */
    { "suspend",
      HPV_ST_ENABLED, HPV_EV_SUSPEND, 0, 0, 0,
      HPV_ST_SUSPENDED, HPV_ACT_SET_SUSPEND, XHCI_SPEED_UNKNOWN },
    { "host-initiated resume",
      HPV_ST_SUSPENDED, HPV_EV_RESUME, 0, 0, 0,
      HPV_ST_RESUMING, HPV_ACT_CLEAR_SUSPEND, XHCI_SPEED_UNKNOWN },
    { "resume complete: C_PORT_SUSPEND, then TRSMRCY",
      HPV_ST_RESUMING, HPV_EV_STATUS, 0, HPV_P | HPV_C | HPV_E,
      XHCI_HUB_C_PORT_SUSPEND,
      HPV_ST_RESUME_RECOVERY,
      HPV_ACT_CLEAR_C_SUSPEND | HPV_ACT_TIMER_RECOVERY, XHCI_SPEED_UNKNOWN },
    { "device-initiated resume (remote wake)",
      HPV_ST_SUSPENDED, HPV_EV_STATUS, 0, HPV_P | HPV_C | HPV_E,
      XHCI_HUB_C_PORT_SUSPEND,
      HPV_ST_RESUME_RECOVERY,
      HPV_ACT_CLEAR_C_SUSPEND | HPV_ACT_TIMER_RECOVERY, XHCI_SPEED_UNKNOWN },
    { "resume recovery over",
      HPV_ST_RESUME_RECOVERY, HPV_EV_TIMER, 0, 0, 0,
      HPV_ST_ENABLED, 0, XHCI_SPEED_UNKNOWN },
    { "unplugged while suspended",
      HPV_ST_SUSPENDED, HPV_EV_STATUS, 0, HPV_P, XHCI_HUB_C_PORT_CONNECTION,
      HPV_ST_DISCONNECTED,
      HPV_ACT_CLEAR_C_CONNECTION | HPV_ACT_REPORT_GONE, XHCI_SPEED_UNKNOWN },
    { "unplugged while resuming",
      HPV_ST_RESUMING, HPV_EV_STATUS, 0, HPV_P, XHCI_HUB_C_PORT_CONNECTION,
      HPV_ST_DISCONNECTED,
      HPV_ACT_CLEAR_C_CONNECTION | HPV_ACT_REPORT_GONE, XHCI_SPEED_UNKNOWN },

    /* Disabled: left so until the next connect change (10.2 step 7). */
    { "connect change on a disabled port: start over",
      HPV_ST_DISABLED, HPV_EV_STATUS, 0, HPV_P | HPV_C,
      XHCI_HUB_C_PORT_CONNECTION,
      HPV_ST_DEBOUNCE, HPV_ACT_CLEAR_C_CONNECTION | HPV_ACT_TIMER_DEBOUNCE,
      XHCI_SPEED_UNKNOWN },
    { "the given-up device unplugged",
      HPV_ST_DISABLED, HPV_EV_STATUS, 0, HPV_P, XHCI_HUB_C_PORT_CONNECTION,
      HPV_ST_DISCONNECTED, HPV_ACT_CLEAR_C_CONNECTION, XHCI_SPEED_UNKNOWN }
};

#define HPV_ROWS (sizeof(hpvRows) / sizeof(hpvRows[0]))

#endif /* XHCI_HUB_PORT_VECTORS_H */
