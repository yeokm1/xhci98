/*
 * test_trrst.c - the USB Tree's restart of a controller, the pure half
 * (xhcitr\tr_rst.c; roadmap-usb4 task 50.32, design record 25 section 27
 * item 14, stage 1): the start's evidence from the lifetime part, the
 * four-part answer row by row and its exit codes, W2 and W4 beside every
 * start answer, W5 after a refusal, the list's completeness rule and its
 * groups, the digest, the run's outcomes (A14, A15, A16, A23), the
 * checks' order; and stage 2, 98 SE and ME: line 2 from the removal, the
 * 9x answers and exit codes, the storage rule, the drive rule (R13 for a
 * letter no devnode claims, owner 2026-11-19), the network rule.
 */

#include <stdio.h>
#include <string.h>
#include "../xhcitr/tr_rst.h"
#include "../xhciinst/inst_core.h"
#include "../src/xhci_qry.h"

static int checks;
static int failures;

#define CHECK(c, what)                                                       \
    do {                                                                     \
        checks++;                                                            \
        if (!(c)) {                                                          \
            failures++;                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what);            \
        }                                                                    \
    } while (0)

static TR_XHR sample(TR_U32 lo, TR_U32 hi, TR_U32 start, int started)
{
    TR_XHR x;

    memset(&x, 0, sizeof(x));
    x.LifeOk = 1;
    x.LifeTimeLo = lo;
    x.LifeTimeHi = hi;
    x.LifeStart = start;
    x.Flags = started ? XHR_F_STARTED : 0;
    return x;
}

static void test_hist(void)
{
    TR_RST_HIST h;
    TR_XHR b = sample(100, 1, 5, 1);
    TR_XHR s;

    tr_rst_hist_init(&h, NULL);
    s = sample(100, 1, 6, 1);
    tr_rst_hist_add(&h, &s);
    CHECK(tr_rst_start(&h) == TR_RST_S4, "no baseline: cannot tell");

    tr_rst_hist_init(&h, &b);
    CHECK(tr_rst_start(&h) == TR_RST_S4, "no sample: cannot tell");
    tr_rst_hist_add(&h, NULL);
    CHECK(tr_rst_start(&h) == TR_RST_S4, "only failed reads: cannot tell");
    tr_rst_hist_add(&h, &b);
    CHECK(tr_rst_start(&h) == TR_RST_S3, "equal identity: no new start seen");
    s = sample(100, 1, 6, 0);
    tr_rst_hist_add(&h, &s);
    CHECK(tr_rst_start(&h) == TR_RST_S2, "another number, not started: S2");
    s = sample(100, 1, 6, 1);
    tr_rst_hist_add(&h, &s);
    CHECK(tr_rst_start(&h) == TR_RST_S1, "another number, started: S1");
    tr_rst_hist_add(&h, NULL);
    s = sample(100, 1, 6, 0);
    tr_rst_hist_add(&h, &s);
    CHECK(tr_rst_start(&h) == TR_RST_S1,
          "S1 kept after a failed read and a stop");

    tr_rst_hist_init(&h, &b);
    s = sample(101, 1, 5, 1);
    tr_rst_hist_add(&h, &s);
    CHECK(tr_rst_start(&h) == TR_RST_S1,
          "a reload: another image time, the same number");
    tr_rst_hist_init(&h, &b);
    s = sample(100, 1, 0, 1);
    tr_rst_hist_add(&h, &s);
    CHECK(tr_rst_start(&h) == TR_RST_S4,
          "number 0 only: no start begun, no valid sample");
    s = b;
    s.LifeOk = 0;
    tr_rst_hist_init(&h, &b);
    tr_rst_hist_add(&h, &s);
    CHECK(tr_rst_start(&h) == TR_RST_S4, "no lifetime part: cannot tell");
    CHECK(h.NoPart == 1, "... counted as an answer without the part");
    s = sample(100, 1, 0, 1);
    tr_rst_hist_init(&h, &s);
    CHECK(!h.BaseValid, "a baseline numbered 0 is not valid");
    s = sample(100, 1, 0xFFFFFFFFUL, 1);
    tr_rst_hist_init(&h, &s);
    b = sample(100, 1, 1, 1);
    tr_rst_hist_add(&h, &b);
    CHECK(tr_rst_start(&h) == TR_RST_S1,
          "the counter's last value, then a wrapped number: another start");
}

static void in_base(TR_RST_IN *in)
{
    memset(in, 0, sizeof(*in));
    in->Name = "xHCI98 USB 3 Controller";
    in->Request = TR_RST_REQ_OK;
    in->Start = TR_RST_S1;
    in->Dn = TR_RST_DN_STARTED;
    in->XhrOk = 1;
    in->Xhr.Flags = XHR_F_STARTED;
    in->Xhr.Terminal = 0;
    in->F = TR_RST_NO;
    in->P = TR_RST_NO;
    in->MarkEnd = TR_RST_NO;
    in->MarkBase = TR_RST_NO;
}

static int answer(const TR_RST_IN *in, char *buf, size_t cap)
{
    TR_TEXT t;

    tr_text_init(&t, buf, cap);
    return tr_rst_answer(in, &t);
}

static void test_answer(void)
{
    TR_RST_IN in;
    char buf[2048];
    int rc;

    in_base(&in);
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_DONE, "accepted, S1, N3, nothing more: exit 0");
    CHECK(strstr(buf, "Windows accepted the restart request for xHCI98 USB 3 "
                      "Controller.\r\nIt has started again.\r\nIt is "
                      "running.\r\n")
              != NULL, "A1, S1, N3 word for word");
    CHECK(strstr(buf, "computer") == NULL, "no line 4");

    in_base(&in);
    in.Request = TR_RST_REQ_FAILED;
    in.Error = 0xE000020BUL;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED,
          "A9 beside S1 (another process's restart): exit 2");
    CHECK(strstr(buf, "did not accept the restart request (error 0xE000020B)")
              != NULL && strstr(buf, "It has started again.") != NULL,
          "... both said");

    in_base(&in);
    in.Request = TR_RST_REQ_DENIED;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_REFUSED, "R1 from the call: exit 1");
    CHECK(strstr(buf, TR_RST_R1) != NULL && strstr(buf, "started again") ==
              NULL, "... R1, no line 2");
    CHECK(strstr(buf, "It is running.") != NULL, "... line 3 still made");

    in_base(&in);
    in.Request = TR_RST_REQ_NODEV;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_REFUSED, "R10: exit 1");
    CHECK(strstr(buf, "is no longer present; nothing was restarted.") != NULL,
          "... R10's words");

    in_base(&in);
    in.Request = TR_RST_REQ_PREP;
    in.Error = 5;
    in.F = TR_RST_NOTREAD;
    in.P = TR_RST_NOTREAD;
    in.Start = TR_RST_S3;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED, "A18: exit 2");
    CHECK(strstr(buf, "could not prepare the restart request (error "
                      "0x00000005); nothing was asked of Windows.") != NULL,
          "... A18's words");
    CHECK(strstr(buf, "The tool saw no new start of it.") != NULL,
          "... line 2 made after A18");
    CHECK(strstr(buf, "could not read whether") == NULL,
          "... no W2 or W4 where neither read was made");

    in_base(&in);
    in.F = TR_RST_YES;
    in.Start = TR_RST_S3;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_RESTART, "W1: exit 3");
    CHECK(strstr(buf, "The tool saw no new start of it.\r\nIt is running.\r\n"
                      "Windows asks for the computer to be restarted.\r\n") !=
              NULL, "... S3, N3, W1");
    CHECK(strstr(buf, "in use") == NULL,
          "... no cause asserted (H1 offers one as a possibility)");

    in_base(&in);
    in.F = TR_RST_YES;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_RESTART, "W1 after S1: exit 3");

    in_base(&in);
    in.F = TR_RST_UNKNOWN;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED, "W2: exit 2");
    CHECK(strstr(buf, "could not read whether Windows asks for the computer "
                      "to be restarted.") != NULL, "... W2's words");

    in_base(&in);
    in.P = TR_RST_YES;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_RESTART, "W3: exit 3");
    CHECK(strstr(buf, "had already asked for the computer to be restarted "
                      "before this request.") != NULL, "... W3's words");

    in_base(&in);
    in.P = TR_RST_UNKNOWN;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED, "W4: exit 2");
    CHECK(strstr(buf, "had asked for the computer to be restarted before this "
                      "request.") != NULL, "... W4's words");

    in_base(&in);
    in.MarkEnd = TR_RST_YES;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_RESTART, "W5: exit 3");
    CHECK(strstr(buf, "Windows has marked xHCI98 USB 3 Controller to be "
                      "finished at the next restart of the computer.") != NULL,
          "... W5's words");

    in_base(&in);
    in.MarkBase = TR_RST_YES;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_RESTART, "W6 with a clear end: exit 3");
    CHECK(strstr(buf, "before this request.") != NULL, "... W6 said");

    in_base(&in);
    in.MarkEnd = TR_RST_UNKNOWN;
    in.MarkBase = TR_RST_UNKNOWN;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED, "W7: exit 2");
    CHECK(strstr(buf, "could not read whether Windows has marked") != NULL,
          "... W7's words");
    in.MarkBase = TR_RST_NO;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_DONE, "the end unread, the baseline clear: no line");

    in_base(&in);
    in.Dn = TR_RST_DN_GONE;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED && strstr(buf, "Windows does not list it "
                                                "now.") != NULL, "N1");
    in.Dn = TR_RST_DN_UNREAD;
    in.DnCode = 0x1F;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED &&
              strstr(buf, "could not read Windows' state of it (code "
                          "0x0000001F).") != NULL, "N6, apart from N1");
    in.Dn = TR_RST_DN_STOPPED;
    in.Problem = 10;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED &&
              strstr(buf, "Windows shows it stopped (problem 10).") != NULL,
          "N2");
    in_base(&in);
    in.XhrOk = 0;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED && strstr(buf, "the tool could not read the "
                                                "driver's state.") != NULL,
          "N5");
    in_base(&in);
    in.Xhr.Flags = 0;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED &&
              strstr(buf, "the driver reports it is not running.") != NULL,
          "N7");
    in_base(&in);
    in.Xhr.Terminal = 1;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED &&
              strstr(buf, "with a recovery of the controller pending.") !=
                  NULL, "N9");
    in.Xhr.Terminal = 3;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED &&
              strstr(buf, "It is stopped after repeated failures.") != NULL,
          "N4");
    in.Xhr.Terminal = 5;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(strstr(buf, "Closed off.") != NULL, "a contained terminal's text");
    in.Xhr.Terminal = 6;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(strstr(buf, "interrupts were lost") != NULL, "terminal 6's text");
    in.Xhr.Terminal = 9;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(strstr(buf, "a state this tool does not know (9).") != NULL,
          "N8 for an unknown terminal");
    in_base(&in);
    in.Start = TR_RST_S2;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED &&
              strstr(buf, "A new start of it began, but the tool did not see "
                          "it running.") != NULL, "S2: exit 2");
    in.Start = TR_RST_S4;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED &&
              strstr(buf, "cannot tell whether it was restarted.") != NULL,
          "S4: exit 2");
}

/* Item 8: W2 and W4 beside every line 2, and W5 after a refusal. */
static void test_line4(void)
{
    TR_RST_IN in;
    char buf[2048];
    int s;
    int rc;
    TR_TEXT t;

    for (s = TR_RST_S1; s <= TR_RST_S4; s++) {
        in_base(&in);
        in.Start = s;
        in.F = TR_RST_UNKNOWN;
        in.P = TR_RST_UNKNOWN;
        rc = answer(&in, buf, sizeof(buf));
        CHECK(rc == INST_EXIT_FAILED, "W2 and W4 with each line 2: exit 2");
        CHECK(strstr(buf, "could not read whether Windows asks for the "
                          "computer") != NULL &&
                  strstr(buf, "could not read whether Windows had asked for "
                              "the computer") != NULL,
              "... both said");
    }
    in_base(&in);
    in.Request = TR_RST_REQ_PREP;
    in.Error = 5;
    in.F = TR_RST_NOTREAD;
    in.P = TR_RST_UNKNOWN;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED, "A18 after a failed first read: exit 2");
    CHECK(strstr(buf, "had asked for the computer to be restarted before this "
                      "request.") != NULL &&
              strstr(buf, "asks for the computer") == NULL,
          "... W4 kept, no W2 where no call was made");
    in.P = TR_RST_YES;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED && strstr(buf, "had already asked") != NULL,
          "A18 with P set: W3 said, exit 2");

    tr_text_init(&t, buf, sizeof(buf));
    rc = tr_rst_refused(TR_RST_R7, TR_RST_YES, "X", &t);
    CHECK(rc == INST_EXIT_REFUSED, "a refusal: exit 1");
    CHECK(strcmp(buf, TR_RST_R7 "\r\nWindows has marked X to be finished at "
                      "the next restart of the computer.\r\n") == 0,
          "W5 after a refusal, word for word");
    tr_text_init(&t, buf, sizeof(buf));
    rc = tr_rst_refused(TR_RST_R7, TR_RST_NO, "X", &t);
    CHECK(rc == INST_EXIT_REFUSED && strcmp(buf, TR_RST_R7 "\r\n") == 0,
          "a refusal, no mark: its text alone");
    tr_text_init(&t, buf, sizeof(buf));
    (void)tr_rst_refused(TR_RST_R7, TR_RST_UNKNOWN, "X", &t);
    CHECK(strstr(buf, "marked") == NULL, "a refusal, the mark unread: no W");
}

/* The list (item 6): a tree of nodes built by the vectors. */
#define NODES 16
static TR_RST_NODE nd[NODES];
static int ndN;
static char ids[64][TR_ID_CHARS];

static int node(int parent, const char *id, const char *cls, const char *name,
                const char *place)
{
    TR_RST_NODE *n = &nd[ndN];

    memset(n, 0, sizeof(*n));
    n->Parent = parent;
    n->Id = id;
    n->Cls = cls;
    n->Name = name;
    n->Place = place;
    n->Letters = "";
    n->ReadOk = 1;
    n->Started = 1;
    return ndN++;
}

static int list(int top, int bad, int bound, TR_RST_LIST *l, char *buf,
                size_t cap)
{
    TR_TEXT t;

    memset(l, 0, sizeof(*l));
    l->Ids = ids;
    l->Cap = 64;
    tr_text_init(&t, buf, cap);
    return tr_rst_list(nd, ndN, top, bad, bound, "PCI\\C", l, &t);
}

static void test_list(void)
{
    TR_RST_LIST l;
    char buf[4096];
    char small[40];
    char d1[TR_DIGEST_CHARS];
    int root;
    int ctl;
    int rh;
    int kbd;
    int comp;
    int f0;
    int f1;
    int hub;
    int hub2;
    int stick;
    int disk;
    int other;
    int rc;

    /* root - controller - root hub - a keyboard; a composite device with
     * a HID function and a storage function (disk with E:); a hub with a
     * hub behind it, a stick behind that; outside the controller a
     * started mouse. */
    ndN = 0;
    root = node(-1, "HTREE\\ROOT\\0", "", "root", "");
    ctl = node(root, "PCI\\C", "USB", "ctl", "");
    rh = node(ctl, "USB\\XHCI98_ROOT_HUB\\1", "USB", "root hub", "");
    nd[rh].IsHubNode = 1;
    nd[rh].IsRootHub = 1;
    kbd = node(rh, "USB\\VID_1&PID_1\\1", "Keyboard", "Keyboard dev",
               "port 1");
    comp = node(rh, "USB\\VID_2&PID_2\\1", "USB", "Composite", "port 2");
    f0 = node(comp, "USB\\VID_2&PID_2&MI_00\\1", "HIDClass", "f0", "");
    (void)node(f0, "HID\\VID_2&PID_2\\1", "Mouse", "f0 mouse", "");
    f1 = node(comp, "USB\\VID_2&PID_2&MI_01\\1", "USB", "f1", "");
    disk = node(f1, "USBSTOR\\DISK\\1", "DiskDrive", "f1 disk", "");
    nd[disk].IsDisk = 1;
    nd[disk].Letters = "E";
    hub = node(rh, "USB\\XHCI98_HUB\\1", "USB", "USB hub", "port 4");
    nd[hub].IsHubNode = 1;
    hub2 = node(hub, "USB\\XHCI98_HUB\\2", "USB", "USB hub",
                "port 1 of the hub at port 4");
    nd[hub2].IsHubNode = 1;
    stick = node(hub2, "USB\\VID_3&PID_3\\1", "USB", "Stick",
                 "port 2 of the hub at port 4.1");
    disk = node(stick, "USBSTOR\\DISK\\2", "DiskDrive", "stick disk", "");
    nd[disk].IsDisk = 1;
    nd[disk].Letters = "FG";
    other = node(root, "HID\\OTHER\\1", "Mouse", "other mouse", "");

    rc = list(ctl, 0, 0, &l, buf, sizeof(buf));
    CHECK(rc == TR_RST_LIST_OK, "a complete walk: listed");
    CHECK(strcmp(buf, "Keyboard dev, port 1 - keyboard\r\n"
                      "Composite, port 2 (E:) - mouse, storage\r\n"
                      "USB hub, port 4\r\n"
                      "USB hub, port 1 of the hub at port 4\r\n"
                      "Stick, port 2 of the hub at port 4.1 (F:, G:) - "
                      "storage\r\n") == 0,
          "the groups: a composite device's functions folded into its "
          "line, a hub behind a hub and the devices on both, no root hub "
          "line");
    if (rc == TR_RST_LIST_OK && strstr(buf, "Composite") == NULL) {
        printf("%s\n", buf);
    }
    CHECK(l.Count == 11, "every devnode below the controller in the digest");
    CHECK(l.Lines == 5 && !l.StoppedEmpty && l.HasDisk && l.Kbd && l.Mouse,
          "the facts");
    CHECK(l.KbdElsewhere == 0 && l.MouseElsewhere == 1,
          "C8 for the keyboard alone: a mouse outside");
    strcpy(d1, l.Digest);

    nd[other].ReadOk = 0;
    rc = list(ctl, 0, 0, &l, buf, sizeof(buf));
    CHECK(rc == TR_RST_LIST_OK && l.KbdElsewhere == -1,
          "a failed read outside: listed, C8a");
    nd[other].ReadOk = 1;
    rc = list(ctl, 3, 0, &l, buf, sizeof(buf));
    CHECK(rc == TR_RST_LIST_OK && l.KbdElsewhere == -1,
          "the walk's failures counted: C8a");
    CHECK(strcmp(l.Digest, d1) == 0, "... the same digest");

    rc = list(ctl, 0, 1, &l, buf, sizeof(buf));
    CHECK(rc == TR_RST_LIST_INCOMPLETE, "a bound met: R11");
    nd[f1].ReadOk = 0;
    rc = list(ctl, 0, 0, &l, buf, sizeof(buf));
    CHECK(rc == TR_RST_LIST_INCOMPLETE, "a failed read below: R11");
    nd[f1].ReadOk = 1;
    nd[hub2].Incomplete = 1;
    rc = list(ctl, 0, 0, &l, buf, sizeof(buf));
    CHECK(rc == TR_RST_LIST_INCOMPLETE, "children not all read below: R11");
    nd[hub2].Incomplete = 0;
    nd[ctl].Incomplete = 1;
    rc = list(ctl, 0, 0, &l, buf, sizeof(buf));
    CHECK(rc == TR_RST_LIST_INCOMPLETE, "the controller's children: R11");
    nd[ctl].Incomplete = 0;

    rc = list(ctl, 0, 0, &l, small, sizeof(small));
    CHECK(rc == TR_RST_LIST_INCOMPLETE, "a list that does not fit: never cut");
    rc = list(ctl, 0, 0, &l, buf, sizeof(buf));
    l.Cap = 3;
    {
        TR_TEXT t;
        tr_text_init(&t, buf, sizeof(buf));
        rc = tr_rst_list(nd, ndN, ctl, 0, 0, "PCI\\C", &l, &t);
    }
    CHECK(rc == TR_RST_LIST_INCOMPLETE, "more ids than room: R11");

    nd[stick].Id = "USB\\VID_3&PID_3\\9";
    rc = list(ctl, 0, 0, &l, buf, sizeof(buf));
    CHECK(rc == TR_RST_LIST_OK && strcmp(l.Digest, d1) != 0,
          "one devnode deep behind two hubs changed: another digest");

    /* Only the root hub: C3. */
    ndN = 0;
    root = node(-1, "HTREE\\ROOT\\0", "", "root", "");
    ctl = node(root, "PCI\\C", "USB", "ctl", "");
    rh = node(ctl, "USB\\XHCI98_ROOT_HUB\\1", "USB", "root hub", "");
    nd[rh].IsHubNode = 1;
    nd[rh].IsRootHub = 1;
    rc = list(ctl, 0, 0, &l, buf, sizeof(buf));
    CHECK(rc == TR_RST_LIST_OK && l.StoppedEmpty && l.Lines == 0 &&
              l.Count == 1 && buf[0] == '\0', "the root hub alone: C3");
    /* Nothing below it at all: not C3's "removed" claim either way. */
    ndN = 2;
    rc = list(ctl, 0, 0, &l, buf, sizeof(buf));
    CHECK(rc == TR_RST_LIST_OK && l.StoppedEmpty && l.Count == 0,
          "no devnode below: C3");
    /* A devnode below that is no USB device's: listed by name. */
    ndN = 0;
    root = node(-1, "HTREE\\ROOT\\0", "", "root", "");
    ctl = node(root, "PCI\\C", "USB", "ctl", "");
    (void)node(ctl, "ROOT\\ODD\\1", "System", "odd", "");
    rc = list(ctl, 0, 0, &l, buf, sizeof(buf));
    CHECK(rc == TR_RST_LIST_OK && !l.StoppedEmpty &&
              strcmp(buf, "odd\r\n") == 0, "a devnode that is no device's");
    rc = list(5, 0, 0, &l, buf, sizeof(buf));
    CHECK(rc == TR_RST_LIST_INCOMPLETE, "the controller not in the walk");
    /* Beside a USB device: every devnode no device owns has its line. */
    rh = node(ctl, "USB\\XHCI98_ROOT_HUB\\1", "USB", "root hub", "");
    nd[rh].IsHubNode = 1;
    nd[rh].IsRootHub = 1;
    kbd = node(rh, "USB\\VID_1&PID_1\\1", "Keyboard", "Keyboard dev",
               "port 1");
    rc = list(ctl, 0, 0, &l, buf, sizeof(buf));
    CHECK(rc == TR_RST_LIST_OK && l.Lines == 2 &&
              strcmp(buf, "Keyboard dev, port 1 - keyboard\r\nodd\r\n") == 0,
          "a devnode no device owns, beside a device: listed too");
    /* The tree's root unread (NT has no entry for it): not a failure. */
    nd[root].ReadOk = 0;
    rc = list(ctl, 0, 0, &l, buf, sizeof(buf));
    CHECK(rc == TR_RST_LIST_OK && l.KbdElsewhere == 0,
          "the root's own entry missing: C8 still");
    nd[root].Incomplete = 1;
    rc = list(ctl, 0, 0, &l, buf, sizeof(buf));
    CHECK(rc == TR_RST_LIST_OK && l.KbdElsewhere == -1,
          "the root's children not all read: C8a");
}

/* Item 9: the elevated or Run As run's outcome in the window. */
static void test_child(void)
{
    CHECK(tr_rst_child(0, 1223, 0, 0, 0, 0) == TR_RST_CH_A14,
          "the prompt or dialog cancelled: A14");
    CHECK(tr_rst_child_exit(TR_RST_CH_A14) == INST_EXIT_REFUSED,
          "... exit 1");
    CHECK(tr_rst_child(0, 1058, 0, 0, 0, 0) == TR_RST_CH_A16,
          "the Run As service stopped: A16");
    CHECK(tr_rst_child_exit(TR_RST_CH_A16) == INST_EXIT_FAILED, "... exit 2");
    CHECK(tr_rst_child(1, 0, 1, 1, 1, 0) == TR_RST_CH_A23,
          "exit 1 with no answer: A23");
    CHECK(tr_rst_child_exit(TR_RST_CH_A23) == INST_EXIT_REFUSED,
          "... exit 1");
    CHECK(tr_rst_child(1, 0, 1, 1, 1, 1) == TR_RST_CH_ANSWER,
          "an answer read: shown as written");
    CHECK(tr_rst_child(1, 0, 1, 1, 2, 0) == TR_RST_CH_A15,
          "another exit with no answer: A15");
    CHECK(tr_rst_child(1, 0, 0, 0, 0, 0) == TR_RST_CH_A15,
          "no end within the wait: A15");
    CHECK(tr_rst_child(1, 0, 1, 0, 1, 0) == TR_RST_CH_A15,
          "an exit code not read: A15");
    CHECK(tr_rst_child_exit(TR_RST_CH_A15) == INST_EXIT_FAILED, "... exit 2");
}

static void test_digest(void)
{
    char ids[3][TR_ID_CHARS];
    char other[3][TR_ID_CHARS];
    char d1[TR_DIGEST_CHARS];
    char d2[TR_DIGEST_CHARS];

    strcpy(ids[0], "USB\\VID_0627&PID_0001\\1");
    strcpy(ids[1], "USB\\XHCI98_ROOT_HUB\\0");
    strcpy(ids[2], "HID\\VID_0627&PID_0001\\2");
    strcpy(other[0], "hid\\vid_0627&pid_0001\\2");
    strcpy(other[1], "USB\\VID_0627&PID_0001\\1");
    strcpy(other[2], "USB\\XHCI98_ROOT_HUB\\0");
    tr_rst_digest("PCI\\VEN_1B36&DEV_000D\\X", ids, 3, d1);
    tr_rst_digest("PCI\\VEN_1B36&DEV_000D\\X", other, 3, d2);
    CHECK(strcmp(d1, d2) == 0, "the walk's order and case do not count");
    CHECK(tr_res_digest_ok(d1), "a digest /confirmed accepts");
    strcpy(other[0], "HID\\VID_0627&PID_0001\\3");
    tr_rst_digest("PCI\\VEN_1B36&DEV_000D\\X", other, 3, d2);
    CHECK(strcmp(d1, d2) != 0, "one devnode changed anywhere: another digest");
    tr_rst_digest("PCI\\VEN_1B36&DEV_000D\\X", ids, 2, d2);
    CHECK(strcmp(d1, d2) != 0, "one devnode fewer: another digest");
    tr_rst_digest("PCI\\VEN_1B36&DEV_000D\\Y", ids, 3, d2);
    CHECK(strcmp(d1, d2) != 0, "another controller: another digest");
}

static void test_check(void)
{
    TR_RST_CHECK c;

    memset(&c, 0, sizeof(c));
    c.Routines = 1;
    c.RecState = 1;
    c.Admin = 1;
    c.WalkComplete = 1;
    CHECK(tr_rst_check(&c) == TR_RST_OK, "an administrator, all well");
    c.SetupRunning = 1;
    CHECK(tr_rst_check(&c) == TR_RST_SETUP, "Setup running: R6");
    c.WalkComplete = 0;
    CHECK(tr_rst_check(&c) == TR_RST_SETUP, "... before R11");
    c.SetupRunning = 0;
    CHECK(tr_rst_check(&c) == TR_RST_WALK, "an incomplete walk: R11");
    c.Admin = 0;
    CHECK(tr_rst_check(&c) == TR_RST_NEED_ADMIN, "2000 or XP, no rights: R1");
    c.Elevates = 1;
    CHECK(tr_rst_check(&c) == TR_RST_WALK, "Vista and later: the shield");
    c.Terminal = 4;
    CHECK(tr_rst_check(&c) == TR_RST_CONTAINED, "contained: R2");
    c.Terminal = 5;
    CHECK(tr_rst_check(&c) == TR_RST_CONTAINED, "contained (5): R2");
    c.Terminal = 3;
    CHECK(tr_rst_check(&c) == TR_RST_WALK, "latched: offered");
    c.RecState = 0;
    c.QueryFailed = 1;
    CHECK(tr_rst_check(&c) == TR_RST_UNKNOWN_FAILED,
          "an older driver and a failed controller: R8");
    c.QueryFailed = 0;
    CHECK(tr_rst_check(&c) == TR_RST_WALK,
          "an older driver, running: offered");
    c.Wow64Limited = 1;
    CHECK(tr_rst_check(&c) == TR_RST_WOW64, "WOW64 without XHCITR64: R4");
    c.Routines = 0;
    CHECK(tr_rst_check(&c) == TR_RST_NO_ROUTINE, "a routine missing: R3");
}

static void test_texts(void)
{
    char b[512];

    tr_rst_c1(b, sizeof(b), "X");
    CHECK(strcmp(b, "Restart X?") == 0, "C1");
    tr_rst_r9(b, sizeof(b), "X");
    CHECK(strcmp(b, "The devices on X changed. Nothing was restarted. Look at "
                    "the list again.") == 0, "R9");
    CHECK(tr_rst_is_r9(b, "X"), "R9 told in a run's answer");
    CHECK(!tr_rst_is_r9(b, "Y"), "... for this controller only");
    CHECK(!tr_rst_is_r9("Nothing was restarted: administrator rights were not "
                        "given.", "X"), "... not another answer");
    tr_rst_r11(b, sizeof(b), "X");
    CHECK(strcmp(b, "The tool could not read every device on X, so it does "
                    "not restart it. Try again, or restart it from Device "
                    "Manager.") == 0, "R11");
    tr_rst_a16(b, sizeof(b), 1058);
    CHECK(strcmp(b, "The tool could not start as an administrator (error "
                    "1058). Nothing was restarted.") == 0, "A16");
    CHECK(tr_rst_a17(b, sizeof(b), "ID") == INST_EXIT_FAILED,
          "A17: exit 2");
    CHECK(strcmp(b, "No xhci98 controller has the instance ID ID.") == 0,
          "A17");
}

static void test_args(void)
{
    TR_OPTS o;
    char err[512];
    char a0[] = "XHCITR";
    char a1[] = "/restart";
    char a2[] = "id=PCI\\VEN_1B36&DEV_000D\\3&1";
    char a3[] = "/result";
    char a4[] = "Local\\Xhci98TrAnswer-12-34";
    char a5[] = "/confirmed";
    char a6[] = "0123456789ABCDEF";
    char a7[] = "/eject";
    char a8[] = "id=X";
    char bad[] = "PCI\\VEN_1B36";
    char *v1[3];
    char *v2[7];
    char *v3[5];
    char *v4[3];

    v1[0] = a0;
    v1[1] = a1;
    v1[2] = a2;
    CHECK(tr_parse_args(3, v1, &o, err, sizeof(err)) == 0 && o.Restart &&
              strcmp(o.RestartId, "PCI\\VEN_1B36&DEV_000D\\3&1") == 0,
          "/restart id=");
    v2[0] = a0;
    v2[1] = a1;
    v2[2] = a2;
    v2[3] = a3;
    v2[4] = a4;
    v2[5] = a5;
    v2[6] = a6;
    CHECK(tr_parse_args(7, v2, &o, err, sizeof(err)) == 0 && o.Result &&
              o.Confirmed, "/restart with /result and /confirmed");
    v3[0] = a0;
    v3[1] = a1;
    v3[2] = a2;
    v3[3] = a7;
    v3[4] = a8;
    CHECK(tr_parse_args(5, v3, &o, err, sizeof(err)) == 1,
          "/restart with /eject: each alone");
    v4[0] = a0;
    v4[1] = a1;
    v4[2] = bad;
    CHECK(tr_parse_args(3, v4, &o, err, sizeof(err)) == 1 &&
              strcmp(err, "/restart needs id=<instance id>.") == 0,
          "/restart without id=");
    CHECK(tr_parse_args(2, v4, &o, err, sizeof(err)) == 1,
          "/restart with nothing after it");
}

/* An elevated or Run As run's answer, already CR LF, carried through the
 * answer section and shown by the window: one CR at each line end. */
static void test_carry(void)
{
    char buf[512];
    char out[512];
    char msg[512];
    int code = -1;
    TR_TEXT t;

    tr_text_init(&t, msg, sizeof(msg));
    tr_put(&t, "line one\nline two\n\nline four");
    CHECK(strcmp(msg, "line one\r\nline two\r\n\r\nline four") == 0,
          "the answer as written: CR LF");
    CHECK(tr_res_answer(2, msg, buf, sizeof(buf)) > 0, "the answer section");
    CHECK(tr_res_read_answer(buf, &code, out, sizeof(out)) == 1 && code == 2,
          "read back with its code");
    CHECK(strcmp(out, msg) == 0, "read back as written: no doubled CR");
    CHECK(strstr(buf, "\r\r") == NULL, "no doubled CR in the section");
}

/* ---- 98 SE and ME (stage 2) ------------------------------------------- */

static void test_start9x(void)
{
    TR_RST_HIST h;
    TR_XHR b = sample(100, 1, 5, 1);
    TR_XHR s;

    tr_rst_hist_init(&h, &b);
    CHECK(tr_rst_start9x(1, 1, 1, 1, 0, &h) == TR_RST_S1,
          "removed, gone, found started: S1 without a lifetime sample");
    CHECK(tr_rst_start9x(1, 1, 1, 0, 0, &h) == TR_RST_S2,
          "removed, gone, found, never started: S2");
    CHECK(tr_rst_start9x(1, 1, 0, 0, 0, &h) == TR_RST_S_A11,
          "removed, gone, not found again: A11");
    CHECK(tr_rst_start9x(1, 1, 0, 0, 1, &h) == TR_RST_S4,
          "removed, gone, never located but a locate failed otherwise: S4, "
          "not A11");
    s = sample(100, 1, 6, 1);
    tr_rst_hist_add(&h, &s);
    CHECK(tr_rst_start9x(1, 1, 1, 0, 0, &h) == TR_RST_S1,
          "found, a lifetime S1 seen: S1");
    tr_rst_hist_init(&h, &b);
    CHECK(tr_rst_start9x(1, 0, 1, 1, 0, &h) == TR_RST_S4,
          "the id still found after the removal: the lifetime samples (none)");
    tr_rst_hist_add(&h, &b);
    CHECK(tr_rst_start9x(0, 0, 1, 1, 0, &h) == TR_RST_S3,
          "after A12: the lifetime samples alone (S3)");
    tr_rst_hist_add(&h, &s);
    CHECK(tr_rst_start9x(0, 0, 1, 0, 0, &h) == TR_RST_S1,
          "after A12: the lifetime samples alone (S1)");
}

static void in9x(TR_RST_IN *in)
{
    in_base(in);
    in->Request = TR_RST_REQ_9X_REMOVED;
    in->F = TR_RST_NOTREAD;
    in->P = TR_RST_NOTREAD;
    in->Rescans = 1;
}

static void test_answer9x(void)
{
    TR_RST_IN in;
    char buf[2048];
    int rc;

    in9x(&in);
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_DONE, "A13, S1, N3: exit 0");
    CHECK(strcmp(buf, "Windows removed xHCI98 USB 3 Controller and looked for "
                      "it again.\r\nIt has started again.\r\nIt is running."
                      "\r\n") == 0, "A13, S1, N3: the three lines");

    in9x(&in);
    in.Start = TR_RST_S_A11;
    in.Dn = TR_RST_DN_GONE;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED, "A11: exit 2");
    CHECK(strcmp(buf, "Windows removed xHCI98 USB 3 Controller and looked for "
                      "it again.\r\nThe controller was removed but Windows "
                      "did not find it again. Use Refresh in Device Manager, "
                      "or restart Windows.\r\n") == 0,
          "A11: no line 3");

    in9x(&in);
    in.Rescans = 2;
    in.RescanCode[0] = 0x1F;
    in.RescanCode[1] = 0;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED, "A13 with a failed rescan, S1, N3: exit 2");
    CHECK(strstr(buf, "Windows' search for the controller answered code "
                      "0x0000001F.\r\nIt has started again.") != NULL,
          "A20 after A13, before line 2");
    in.RescanCode[0] = 0;
    in.RootFail = 1;
    in.RootCode = 0x5;
    in.Rescans = 1;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED && strstr(buf, "The tool could not start a "
          "second search of the whole computer (code 0x00000005).") != NULL,
          "A22: exit 2");

    in9x(&in);
    in.Request = TR_RST_REQ_9X_VETO;
    in.Rescans = 0;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_REFUSED, "A10: exit 1");
    CHECK(strcmp(buf, "Windows refused to remove xHCI98 USB 3 Controller. A "
                      "program may be using one of its devices: close the "
                      "programs using its drives and try again.\r\n"
                      "It is running.\r\n") == 0,
          "A10: no line 2, line 3");

    in.Request = TR_RST_REQ_9X_QFAIL;
    in.Error = 0x1C;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_REFUSED && strstr(buf, "Windows could not prepare "
          "the removal of xHCI98 USB 3 Controller (code 0x0000001C). Nothing "
          "was removed.\r\nIt is running.") == buf, "A19: exit 1");

    in.Request = TR_RST_REQ_9X_NOPARENT;
    in.Error = 0x5;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED && strstr(buf, "The tool could not find the "
          "device xHCI98 USB 3 Controller is attached to (code 0x00000005). "
          "Nothing was removed.\r\nIt is running.") == buf, "A21: exit 2");

    in9x(&in);
    in.Request = TR_RST_REQ_9X_RMFAIL;
    in.Error = 0x17;
    in.Start = TR_RST_S3;
    in.RescanCode[0] = 0x2;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED, "A12: exit 2");
    CHECK(strcmp(buf, "Windows accepted the request but could not remove "
                      "xHCI98 USB 3 Controller (code 0x00000017).\r\n"
                      "Windows' search for the controller answered code "
                      "0x00000002.\r\nThe tool saw no new start of it.\r\n"
                      "It is running.\r\n") == 0,
          "A12, its rescan's A20, line 2 from the samples, line 3");

    in9x(&in);
    in.MarkEnd = TR_RST_YES;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_RESTART && strstr(buf, "Windows has marked") != NULL,
          "A13 with W5: exit 3");
    in9x(&in);
    in.MarkEnd = TR_RST_UNKNOWN;
    in.MarkBase = TR_RST_UNKNOWN;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED && strstr(buf, "could not read whether "
          "Windows has marked") != NULL, "A13 with W7: exit 2");
    CHECK(strstr(buf, "asks for the computer") == NULL &&
          strstr(buf, "before this request") == NULL,
          "no W1 to W4 on 98 SE and ME");
    in9x(&in);
    in.Start = TR_RST_S2;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED, "A13 with S2: exit 2");
}

/* Revision 50 (owner, 2026-11-19): H1, the hint after a restart Windows
 * refuses in the session - present, last, with A1, S3 and W1 or W5, and
 * absent in every neighbouring combination; the exit code unchanged; the
 * answer with it too long for the tray's balloon. */
static int has_h1(const char *buf)
{
    return strstr(buf, TR_RST_H1) != NULL;
}

static void test_hint(void)
{
    TR_RST_IN in;
    char buf[2048];
    char ref[2048];
    TR_TEXT t;
    int rc;
    int s;
    size_t n;

    /* The refusal as read on Vista: A1, S3, N3, W1, W5. */
    in_base(&in);
    in.Name = "xHCI98 USB 3 Controller (Red Hat QEMU xHCI)";
    in.Start = TR_RST_S3;
    in.F = TR_RST_YES;
    in.MarkEnd = TR_RST_YES;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_RESTART, "A1 S3 W1 W5 with H1: exit 3, unchanged");
    CHECK(strcmp(buf,
                 "Windows accepted the restart request for xHCI98 USB 3 "
                 "Controller (Red Hat QEMU xHCI).\r\n"
                 "The tool saw no new start of it.\r\n"
                 "It is running.\r\n"
                 "Windows asks for the computer to be restarted.\r\n"
                 "Windows has marked xHCI98 USB 3 Controller (Red Hat QEMU "
                 "xHCI) to be finished at the next restart of the computer."
                 "\r\n"
                 "A program may be using one of its drives: close it and try "
                 "again, or restart the computer.\r\n") == 0,
          "A1 S3 N3 W1 W5, then H1 last, word for word");
    CHECK(tr_rst_exit(&in) == INST_EXIT_RESTART, "... tr_rst_exit agrees");
    CHECK(!tr_rst_in_balloon(buf), "... too long for the tray's balloon: "
                                   "the message box");

    /* The same with the earlier mark too (W6): H1 still last. */
    in.MarkBase = TR_RST_YES;
    rc = answer(&in, buf, sizeof(buf));
    n = strlen(buf);
    CHECK(rc == INST_EXIT_RESTART && has_h1(buf) &&
              n > strlen(TR_RST_H1) + 2 &&
              strcmp(buf + n - strlen(TR_RST_H1) - 2, TR_RST_H1 "\r\n") == 0,
          "W1 W5 W6: H1 after W6, the last line");

    /* W1 alone, W5 alone. */
    in_base(&in);
    in.Start = TR_RST_S3;
    in.F = TR_RST_YES;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_RESTART && has_h1(buf), "A1 S3 W1: H1, exit 3");
    in_base(&in);
    in.Start = TR_RST_S3;
    in.MarkEnd = TR_RST_YES;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_RESTART && has_h1(buf), "A1 S3 W5: H1, exit 3");

    /* Another line 2 beside W1 and W5: no H1. */
    for (s = TR_RST_S1; s <= TR_RST_S4; s++) {
        if (s == TR_RST_S3) {
            continue;
        }
        in_base(&in);
        in.Start = s;
        in.F = TR_RST_YES;
        in.MarkEnd = TR_RST_YES;
        rc = answer(&in, buf, sizeof(buf));
        CHECK(rc == INST_EXIT_RESTART && !has_h1(buf),
              "S1, S2 or S4 with W1 and W5: no H1, exit 3");
    }

    /* S3 with neither W1 nor W5: no H1, whatever else line 4 holds. */
    in_base(&in);
    in.Start = TR_RST_S3;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED && !has_h1(buf), "A1 S3, no line 4: no H1");
    in.P = TR_RST_YES;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_RESTART && !has_h1(buf), "A1 S3 W3 alone: no H1");
    in.P = TR_RST_NO;
    in.MarkBase = TR_RST_YES;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_RESTART && !has_h1(buf), "A1 S3 W6 alone: no H1");
    in.MarkBase = TR_RST_NO;
    in.F = TR_RST_UNKNOWN;
    in.P = TR_RST_UNKNOWN;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED && !has_h1(buf), "A1 S3 W2 W4: no H1");
    in.F = TR_RST_NO;
    in.P = TR_RST_NO;
    in.MarkEnd = TR_RST_UNKNOWN;
    in.MarkBase = TR_RST_UNKNOWN;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED && !has_h1(buf), "A1 S3 W7: no H1");

    /* A request not accepted, from the call or before it: no H1. */
    in_base(&in);
    in.Start = TR_RST_S3;
    in.F = TR_RST_YES;
    in.MarkEnd = TR_RST_YES;
    in.Request = TR_RST_REQ_FAILED;
    in.Error = 0xE000020BUL;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED && !has_h1(buf), "A9 S3 W1 W5: no H1");
    in.Request = TR_RST_REQ_PREP;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_FAILED && !has_h1(buf), "A18 S3 W1 W5: no H1");
    in.Request = TR_RST_REQ_DENIED;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_REFUSED && !has_h1(buf), "R1 from the call: no H1");
    in.Request = TR_RST_REQ_NODEV;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_REFUSED && !has_h1(buf), "R10: no H1");
    tr_text_init(&t, buf, sizeof(buf));
    rc = tr_rst_refused(TR_RST_R7, TR_RST_YES, "X", &t);
    CHECK(rc == INST_EXIT_REFUSED && !has_h1(buf),
          "a refusal before the call with the mark (W5): no H1");

    /* 98 SE and ME: no H1 with any of their answers. */
    in9x(&in);
    in.Start = TR_RST_S3;
    in.MarkEnd = TR_RST_YES;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_RESTART && !has_h1(buf), "9x A13 S3 W5: no H1");
    in.Request = TR_RST_REQ_9X_RMFAIL;
    (void)answer(&in, buf, sizeof(buf));
    CHECK(!has_h1(buf), "9x A12 S3 W5: no H1");
    in.Request = TR_RST_REQ_9X_VETO;
    rc = answer(&in, buf, sizeof(buf));
    CHECK(rc == INST_EXIT_REFUSED && !has_h1(buf), "9x A10 W5: no H1");

    /* The tray's balloon limit itself. */
    memset(ref, 'x', TR_RST_BALLOON_CHARS);
    ref[TR_RST_BALLOON_CHARS - 1] = '\0';
    CHECK(tr_rst_in_balloon(ref), "249 characters: the balloon");
    ref[TR_RST_BALLOON_CHARS - 1] = 'x';
    ref[TR_RST_BALLOON_CHARS] = '\0';
    CHECK(!tr_rst_in_balloon(ref), "250 characters: the message box");
    CHECK(!tr_rst_in_balloon(NULL), "no text: not the balloon");
    in_base(&in);
    (void)answer(&in, buf, sizeof(buf));
    CHECK(tr_rst_in_balloon(buf), "A1 S1 N3: the balloon");
}

static void test_storage9x(void)
{
    TR_RST_NODE n;
    unsigned char cfg[32];
    static const char hw[] = "USB\\VID_1&PID_2&REV_0100\0USB\\VID_1&PID_2\0";
    static const char cp8[] = "USB\\Class_08&SubClass_06&Prot_50\0"
                              "USB\\Class_08&SubClass_06\0USB\\Class_08\0";
    static const char cpsub[] = "USB\\Class_FF&SubClass_08&Prot_00\0"
                                "USB\\Class_FF\0";

    memset(&n, 0, sizeof(n));
    n.Id = "USB\\VID_1&PID_2\\1";
    n.Cls = "USB";
    n.Hw = hw;
    n.HwBytes = sizeof(hw);
    CHECK(!tr_rst_node_storage(&n), "a plain device: not storage");
    n.Compat = cp8;
    n.CompatBytes = sizeof(cp8);
    CHECK(tr_rst_node_storage(&n), "an unbound device with Class_08: storage");
    n.Compat = cpsub;
    n.CompatBytes = sizeof(cpsub);
    CHECK(!tr_rst_node_storage(&n), "SubClass_08 is not Class_08");
    n.Compat = NULL;
    n.CompatBytes = 0;
    n.Cls = "CDROM";
    CHECK(tr_rst_node_storage(&n), "class CDROM");
    n.Cls = "hdc";
    CHECK(tr_rst_node_storage(&n), "class hdc");
    n.Cls = "SCSIAdapter";
    CHECK(tr_rst_node_storage(&n), "class SCSIAdapter");
    n.Cls = "diskdrive";
    CHECK(tr_rst_node_storage(&n), "class DiskDrive, any case");
    n.Cls = "USB";
    n.Id = "USBSTOR\\DISK&VEN_X\\1";
    CHECK(tr_rst_node_storage(&n), "an id USBSTOR\\");
    n.Id = "SCSI\\QEMU\\1";
    CHECK(tr_rst_node_storage(&n), "an id SCSI\\");
    n.Id = "USB\\VID_1&PID_2\\1";
    n.Service = "USBNTMAP.SYS";
    CHECK(tr_rst_node_storage(&n), "service USBNTMAP (its file name)");
    n.Service = "xhciuas";
    CHECK(tr_rst_node_storage(&n), "service xhciuas");
    n.Service = "usbstor";
    CHECK(tr_rst_node_storage(&n), "service USBSTOR");
    n.Service = "usbstorx";
    CHECK(!tr_rst_node_storage(&n), "another service");

    memset(cfg, 0, sizeof(cfg));
    cfg[0] = 9; cfg[1] = 2; cfg[2] = 32; cfg[4] = 1;
    cfg[9] = 9; cfg[10] = 4; cfg[14] = 0x08;
    cfg[18] = 7; cfg[19] = 5;
    cfg[25] = 7; cfg[26] = 5;
    CHECK(tr_rst_cfg_storage(cfg, 32) == 1, "an interface 08h: storage");
    cfg[14] = 0x03;
    CHECK(tr_rst_cfg_storage(cfg, 32) == 0, "an interface 03h: not storage");
    cfg[4] = 2;
    CHECK(tr_rst_cfg_storage(cfg, 32) == -1,
          "two interfaces declared, one there: -1");
    cfg[4] = 1;
    cfg[18] = 9; cfg[19] = 4; cfg[20] = 0; cfg[21] = 1; cfg[23] = 0x08;
    cfg[27] = 5; cfg[28] = 5;
    CHECK(tr_rst_cfg_storage(cfg, 32) == 1,
          "an alternate setting of class 08h: storage, not counted as an "
          "interface");
    memset(cfg, 0, sizeof(cfg));
    cfg[0] = 9; cfg[1] = 2; cfg[2] = 11; cfg[4] = 0;
    cfg[9] = 2; cfg[10] = 4;
    CHECK(tr_rst_cfg_storage(cfg, 11) == -1,
          "an interface descriptor of 2 bytes: -1");
    memset(cfg, 0, sizeof(cfg));
    cfg[0] = 9; cfg[1] = 2; cfg[2] = 32; cfg[4] = 1;
    cfg[9] = 9; cfg[10] = 4; cfg[14] = 0x03;
    cfg[18] = 7; cfg[19] = 5;
    cfg[25] = 7; cfg[26] = 5;
    CHECK(tr_rst_cfg_storage(cfg, 31) == -1, "the total length not met: -1");
    cfg[18] = 0;
    CHECK(tr_rst_cfg_storage(cfg, 32) == -1, "a descriptor of length 0: -1");
    cfg[18] = 20;
    CHECK(tr_rst_cfg_storage(cfg, 32) == -1, "a descriptor past the end: -1");
    CHECK(tr_rst_cfg_storage(NULL, 0) == -1, "not fetched: -1");
}

/* A machine for item 7's rules: root, the PCI bus, the IDE disk (C:), the
 * controller, its root hub and a keyboard; the drives C: fixed. */
static TR_RST_DRIVES drv;
static unsigned char devDesc[18];
static unsigned char cfgPlain[18];
static unsigned char cfgStor[18];
static TR_RST_PORT prt[4];
static int ctl9;
static int disk9;
static int rh9;

static void machine9x(void)
{
    int root;
    int pci;
    int ide;
    int kbd;

    ndN = 0;
    root = node(-1, "HTREE\\ROOT\\0", "", "root", "");
    pci = node(root, "PCI\\BUS", "System", "PCI bus", "");
    ide = node(pci, "PCI\\IDE", "hdc", "IDE controller", "");
    disk9 = node(ide, "ESDI\\DISK\\0", "DiskDrive", "IDE disk", "");
    nd[disk9].LetState = TR_RST_LET_READ;
    nd[disk9].DrvLetters = "C";
    ctl9 = node(pci, "PCI\\VEN_1B36&DEV_000D\\0", "USB", "xHCI98 USB 3 "
                "Controller", "");
    rh9 = node(ctl9, "USB\\XHCI98_ROOT_HUB\\0", "USB", "root hub", "");
    nd[rh9].IsHubNode = 1;
    nd[rh9].IsRootHub = 1;
    kbd = node(rh9, "USB\\VID_1&PID_1\\1", "Keyboard", "USB keyboard",
               "port 1");
    (void)kbd;
    memset(&drv, 0, sizeof(drv));
    drv.WinDir = "C:\\WINDOWS";
    drv.SysDir = "C:\\WINDOWS\\SYSTEM";
    drv.Mask = 1UL << 2;
    drv.Type[2] = 3;
    memset(devDesc, 0, sizeof(devDesc));
    devDesc[0] = 18;
    devDesc[1] = 1;
    memset(cfgPlain, 0, sizeof(cfgPlain));
    cfgPlain[0] = 9; cfgPlain[1] = 2; cfgPlain[2] = 18; cfgPlain[4] = 1;
    cfgPlain[9] = 9; cfgPlain[10] = 4; cfgPlain[14] = 0x03;
    memcpy(cfgStor, cfgPlain, sizeof(cfgStor));
    cfgStor[14] = 0x08;
    memset(prt, 0, sizeof(prt));
    prt[0].Read = 1;
    prt[0].Connected = 1;
    prt[0].Dev = devDesc;
    prt[0].Cfg = cfgPlain;
    prt[0].CfgN = 18;
    prt[0].Node = kbd;
    prt[0].Name = "USB keyboard";
    prt[1].Read = 1;
    prt[1].Node = -1;
}

static int rules(int np, char *why, size_t cap)
{
    return tr_rst_rules9x(&drv, nd, ndN, ctl9, 0, 0, prt, np, 0,
                          "xHCI98 USB 3 Controller", why, cap, NULL);
}

/* Shift-JIS's lead bytes, for the refusal's cut between characters
 * (record 25 section 28 item 11). */
static int rst_sjis(unsigned char c)
{
    return (c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC);
}

static void test_rules9x(void)
{
    char why[2048];
    int r;
    int stick;
    int sdisk;
    int net;

    machine9x();
    r = rules(2, why, sizeof(why));
    CHECK(r == 0 && why[0] == '\0',
          "every letter placed off the controller, a keyboard on it: passes");

    /* An unclaimed letter (owner, 2026-11-19): R13 whatever is on it. */
    nd[disk9].LetState = TR_RST_LET_ABSENT;
    r = rules(2, why, sizeof(why));
    CHECK(r == 13 && strcmp(why, "The tool could not tell which controller "
          "drive C: is on, so it does not restart this controller. Restart "
          "Windows instead.") == 0, "C: unclaimed, a keyboard on it: R13");
    r = rules(0, why, sizeof(why));
    CHECK(r == 13, "C: unclaimed, nothing read of the ports: R13");
    nd[disk9].LetState = TR_RST_LET_READ;
    drv.Mask |= 1UL << 3;
    drv.Type[3] = 3;
    stick = node(rh9, "USB\\VID_9&PID_9\\1", "USB", "Stick", "port 2");
    sdisk = node(stick, "USBSTOR\\DISK\\9", "DiskDrive", "stick disk", "");
    nd[sdisk].LetState = TR_RST_LET_READ;
    nd[sdisk].DrvLetters = "E";
    drv.Mask |= 1UL << 4;
    drv.Type[4] = 2;
    r = rules(2, why, sizeof(why));
    CHECK(r == 13 && strstr(why, "drive D:") != NULL,
          "D: unclaimed beside a placed E: on the controller and a stick: "
          "R13 first, naming D:");
    drv.Mask &= ~(1UL << 3);
    r = rules(2, why, sizeof(why));
    CHECK(r == 18 && strcmp(why, "Drive E: is on xHCI98 USB 3 Controller, so "
          "the controller is not restarted. Unplug the drive's device, and "
          "then restart the controller.") == 0,
          "E: placed on the controller: R18 before R12");
    nd[sdisk].DrvLetters = "CE";
    r = rules(2, why, sizeof(why));
    CHECK(r == 13 && strstr(why, "drive C:") != NULL,
          "C: claimed twice: R13 naming C:");
    nd[disk9].DrvLetters = "";
    r = rules(2, why, sizeof(why));
    CHECK(r == 5 && strcmp(why, "Windows uses drive C: on this controller "
          "(the Windows folder), so the controller cannot be restarted "
          "while Windows runs. Restart Windows instead.") == 0,
          "the Windows drive on the controller: R5 before R18");
    drv.WinDir = "D:\\WINDOWS";
    drv.Mask |= 1UL << 3;
    drv.Type[3] = 3;
    nd[disk9].DrvLetters = "D";
    r = rules(2, why, sizeof(why));
    CHECK(r == 5 && strstr(why, "(the system folder)") != NULL,
          "the system folder's drive on it: R5 naming the system folder");
    drv.WinDir = "C:\\WINDOWS";
    drv.Mask &= ~(1UL << 3);
    nd[disk9].DrvLetters = "C";
    nd[sdisk].DrvLetters = "";
    nd[sdisk].LetState = TR_RST_LET_ABSENT;
    drv.Mask &= ~(1UL << 4);
    r = rules(2, why, sizeof(why));
    CHECK(r == 12 && strcmp(why, "USB storage is connected to xHCI98 USB 3 "
          "Controller: Stick, port 2. Unplug it, and then restart the "
          "controller.") == 0, "a stick with no letter: R12 by its devnodes");

    /* The faults that are no one letter's: R13 naming no letter. */
    machine9x();
    drv.WinDir = NULL;
    r = rules(2, why, sizeof(why));
    CHECK(r == 13 && strcmp(why, "The tool could not tell which controller "
          "the Windows drive is on, so it does not restart this controller. "
          "Restart Windows instead.") == 0, "step 1 failed: R13, no letter");
    drv.WinDir = "WINDOWS";
    r = rules(2, why, sizeof(why));
    CHECK(r == 13 && strstr(why, "the Windows drive") != NULL,
          "a path not X:\\: R13, no letter");
    drv.WinDir = "\\\\SERVER\\WIN";
    drv.SysDir = "\\\\SERVER\\WIN\\SYSTEM";
    r = rules(2, why, sizeof(why));
    CHECK(r == 0, "a network installation: no failure, passes with no "
          "network adapter on the controller");
    net = node(rh9, "USB\\VID_7&PID_7\\1", "Net", "USB Ethernet", "port 3");
    r = rules(2, why, sizeof(why));
    CHECK(r == 15 && strcmp(why, "A network adapter on xHCI98 USB 3 "
          "Controller may be carrying a network drive Windows uses, so the "
          "controller is not restarted. Disconnect the network drives first, "
          "or restart Windows.") == 0,
          "a network installation and a network adapter on it: R15");
    machine9x();
    net = node(rh9, "USB\\VID_7&PID_7\\1", "Net", "USB Ethernet", "port 3");
    r = rules(2, why, sizeof(why));
    CHECK(r == 0, "a network adapter, no network letter: passes");
    drv.Mask |= 1UL << 25;
    drv.Type[25] = 4;
    r = rules(2, why, sizeof(why));
    CHECK(r == 15, "a network letter Z: and a network adapter on it: R15");
    nd[net].Cls = "USB";
    r = rules(2, why, sizeof(why));
    CHECK(r == 0, "a network letter, no adapter on the controller: passes");
    machine9x();
    drv.Mask = 0;
    r = rules(2, why, sizeof(why));
    CHECK(r == 13 && strstr(why, "the Windows drive") != NULL,
          "GetLogicalDrives answering 0: R13, no letter");
    machine9x();
    r = tr_rst_rules9x(&drv, nd, ndN, ctl9, 1, 0, prt, 2, 0, "X", why,
                       sizeof(why), NULL);
    CHECK(r == 13 && strstr(why, "the Windows drive") != NULL,
          "the walk's failures: R13, no letter");
    r = tr_rst_rules9x(&drv, nd, ndN, ctl9, 0, 1, prt, 2, 0, "X", why,
                       sizeof(why), NULL);
    CHECK(r == 13, "the walk's bound met: R13");
    nd[rh9].LetState = TR_RST_LET_FAILED;
    r = rules(2, why, sizeof(why));
    CHECK(r == 13 && strstr(why, "the Windows drive") != NULL,
          "an assignment not read: R13, no letter, before a letter's");
    nd[rh9].LetState = TR_RST_LET_ABSENT;
    nd[rh9].ReadOk = 0;
    r = rules(2, why, sizeof(why));
    CHECK(r == 13, "a devnode not read: R13");
    nd[rh9].ReadOk = 1;
    nd[0].LetState = TR_RST_LET_FAILED;
    nd[0].ReadOk = 0;
    r = rules(2, why, sizeof(why));
    CHECK(r == 0, "the tree's root without its Enum key: no failure");
    machine9x();
    drv.Mask |= 1UL << 7;
    drv.Type[7] = 1;
    r = rules(2, why, sizeof(why));
    CHECK(r == 13 && strstr(why, "drive H:") != NULL,
          "a letter with no root: R13 naming it");
    drv.Type[7] = 6;
    r = rules(2, why, sizeof(why));
    CHECK(r == 13 && strstr(why, "drive H:") != NULL,
          "an unclaimed RAM drive: R13");

    /* A floppy letter no devnode claims (owner, 2026-11-19, revision 49):
     * a removable A: or B: is passed over; nothing else is. */
    machine9x();
    drv.Mask |= 1UL << 0;
    drv.Type[0] = 2;
    r = rules(2, why, sizeof(why));
    CHECK(r == 0 && why[0] == '\0',
          "an unclaimed removable A:, a keyboard on the controller: passes");
    drv.Mask |= 1UL << 1;
    drv.Type[1] = 2;
    r = rules(2, why, sizeof(why));
    CHECK(r == 0, "unclaimed removable A: and B: both: passes");
    drv.Mask &= ~(1UL << 0);
    r = rules(2, why, sizeof(why));
    CHECK(r == 0, "an unclaimed removable B: alone: passes");
    drv.Mask |= 1UL << 0;
    nd[disk9].LetState = TR_RST_LET_ABSENT;
    r = rules(2, why, sizeof(why));
    CHECK(r == 13 && strstr(why, "drive C:") != NULL,
          "unclaimed floppy letters beside an unclaimed C:: R13 naming C:");
    nd[disk9].LetState = TR_RST_LET_READ;
    drv.Type[0] = 3;
    r = rules(2, why, sizeof(why));
    CHECK(r == 13 && strstr(why, "drive A:") != NULL,
          "an unclaimed fixed A:: R13 naming A:");
    drv.Type[0] = 5;
    r = rules(2, why, sizeof(why));
    CHECK(r == 13 && strstr(why, "drive A:") != NULL,
          "an unclaimed CD-ROM A:: R13 naming A:");
    drv.Type[0] = 6;
    r = rules(2, why, sizeof(why));
    CHECK(r == 13 && strstr(why, "drive A:") != NULL,
          "an unclaimed RAM disk A:: R13 naming A:");
    drv.Type[0] = 1;
    r = rules(2, why, sizeof(why));
    CHECK(r == 13 && strstr(why, "drive A:") != NULL,
          "an A: with no root: R13 naming A:");
    drv.Type[0] = 2;
    drv.Mask |= 1UL << 7;
    drv.Type[7] = 2;
    r = rules(2, why, sizeof(why));
    CHECK(r == 13 && strstr(why, "drive H:") != NULL,
          "an unclaimed removable H:: R13 naming H: (only A: and B:)");
    drv.Mask &= ~(1UL << 7);
    nd[disk9].DrvLetters = "CA";
    stick = node(rh9, "USB\\VID_9&PID_9\\1", "USB", "Floppy", "port 2");
    sdisk = node(stick, "USBSTOR\\DISK\\9", "DiskDrive", "floppy disk", "");
    nd[sdisk].LetState = TR_RST_LET_READ;
    nd[sdisk].DrvLetters = "A";
    r = rules(2, why, sizeof(why));
    CHECK(r == 13 && strstr(why, "drive A:") != NULL,
          "a removable A: claimed twice: R13 naming A:");
    nd[disk9].DrvLetters = "C";
    r = rules(2, why, sizeof(why));
    CHECK(r == 18 && strstr(why, "Drive A: is on xHCI98 USB 3 Controller") !=
          NULL, "a removable A: claimed once on the controller: R18");
    nd[sdisk].DrvLetters = "";
    nd[sdisk].LetState = TR_RST_LET_ABSENT;
    r = rules(2, why, sizeof(why));
    CHECK(r == 12 && strstr(why, "Floppy, port 2") != NULL,
          "an unclaimed removable A: beside USB storage on the controller: "
          "R12");
    machine9x();
    drv.Mask |= 1UL << 0;
    drv.Type[0] = 2;
    nd[rh9].LetState = TR_RST_LET_FAILED;
    r = rules(2, why, sizeof(why));
    CHECK(r == 13 && strstr(why, "the Windows drive") != NULL,
          "an assignment not read beside an unclaimed floppy letter: R13, "
          "no letter");
    machine9x();
    drv.Mask |= 1UL << 0;
    drv.Type[0] = 2;
    r = tr_rst_rules9x(&drv, nd, ndN, ctl9, 1, 0, prt, 2, 0, "X", why,
                       sizeof(why), NULL);
    CHECK(r == 13 && strstr(why, "the Windows drive") != NULL,
          "an incomplete walk beside an unclaimed floppy letter: R13, no "
          "letter");

    /* The storage rule by the ports. */
    machine9x();
    prt[1].Connected = 1;
    prt[1].Dev = devDesc;
    prt[1].Cfg = cfgStor;
    prt[1].CfgN = 18;
    prt[1].Name = "USB device 0781:5567, port 2";
    r = rules(2, why, sizeof(why));
    CHECK(r == 12 && strstr(why, ": USB device 0781:5567, port 2. Unplug") !=
          NULL, "a devnode-less stick (stopped with Safely Remove): R12");
    {
        /* Task 50.39 (record 25 section 28 item 11): the refusal cut to its
         * room between characters. */
        static const char pre[] = "USB storage is connected to xHCI98 USB 3 "
                                  "Controller: ";
        char nm[64];
        size_t pl = sizeof(pre) - 1;
        int k;
        for (k = 0; k < 20; k++) {
            nm[2 * k] = (char)0x82;
            nm[2 * k + 1] = (char)0xA0;
        }
        nm[40] = '\0';
        prt[1].Name = nm;
        r = tr_rst_rules9x(&drv, nd, ndN, ctl9, 0, 0, prt, 2, 0,
                           "xHCI98 USB 3 Controller", why, sizeof(why),
                           rst_sjis);
        CHECK(r == 12 && strncmp(why, pre, pl) == 0 &&
              memcmp(why + pl, nm, 40) == 0 &&
              strcmp(why + pl + 40, ". Unplug it, and then restart the "
                     "controller.") == 0, "R12: a double-byte name whole");
        r = tr_rst_rules9x(&drv, nd, ndN, ctl9, 0, 0, prt, 2, 0,
                           "xHCI98 USB 3 Controller", why, pl + 20 + 1,
                           rst_sjis);
        CHECK(r == 12 && strlen(why) == pl + 20 && memcmp(why + pl, nm, 20) == 0,
              "R12: cut at its room at a character's end");
        r = tr_rst_rules9x(&drv, nd, ndN, ctl9, 0, 0, prt, 2, 0,
                           "xHCI98 USB 3 Controller", why, pl + 19 + 1,
                           rst_sjis);
        CHECK(r == 12 && strlen(why) == pl + 18 && memcmp(why + pl, nm, 18) == 0,
              "R12: a byte under a character's end, a byte sooner");
        r = tr_rst_rules9x(&drv, nd, ndN, ctl9, 0, 0, prt, 2, 0,
                           "xHCI98 USB 3 Controller", why, pl + 21 + 1,
                           rst_sjis);
        CHECK(r == 12 && strlen(why) == pl + 20 && memcmp(why + pl, nm, 20) == 0,
              "R12: a byte over a character's end, a byte sooner");
        r = tr_rst_rules9x(&drv, nd, ndN, ctl9, 0, 0, prt, 2, 0,
                           "xHCI98 USB 3 Controller", why, pl + 21 + 1, NULL);
        CHECK(r == 12 && strlen(why) == pl + 21 &&
              (unsigned char)why[pl + 20] == 0x82,
              "R12: without a lead test as before, a lead byte last");
        prt[1].Name = "USB device 0781:5567, port 2";
        r = tr_rst_rules9x(&drv, nd, ndN, ctl9, 0, 0, prt, 2, 0,
                           "xHCI98 USB 3 Controller", why, pl + 21 + 1,
                           rst_sjis);
        {
            char w0[2048];
            int r0 = tr_rst_rules9x(&drv, nd, ndN, ctl9, 0, 0, prt, 2, 0,
                                    "xHCI98 USB 3 Controller", w0, pl + 21 + 1,
                                    NULL);
            CHECK(r == 12 && r0 == 12 && strcmp(why, w0) == 0,
                  "R12: a single-byte name cut as with no test");
        }
    }
    prt[1].Cfg = cfgPlain;
    devDesc[4] = 0x08;
    r = rules(2, why, sizeof(why));
    CHECK(r == 12 && strstr(why, "USB keyboard, port 1; USB device 0781:5567, "
          "port 2") != NULL, "a device class 08h: R12, each named once");
    devDesc[4] = 0;
    prt[1].Cfg = NULL;
    r = rules(2, why, sizeof(why));
    CHECK(r == 16 && strcmp(why, "The tool could not read what is plugged into "
          "xHCI98 USB 3 Controller, so it does not restart it. Try again, or "
          "restart Windows.") == 0, "a configuration not fetched: R16");
    prt[1].Cfg = cfgStor;
    r = rules(2, why, sizeof(why));
    CHECK(r == 12, "R12 before an unread port elsewhere");
    prt[1].Cfg = cfgPlain;
    prt[1].IsHub = 1;
    r = rules(2, why, sizeof(why));
    CHECK(r == 16, "a hub whose ports were not read: R16");
    prt[1].HubRead = 1;
    r = rules(2, why, sizeof(why));
    CHECK(r == 0, "a hub whose ports were read: passes");
    prt[1].Read = 0;
    r = rules(2, why, sizeof(why));
    CHECK(r == 16, "a port whose connection was not read: R16");
    prt[1].Read = 1;
    prt[1].Dev = NULL;
    r = rules(2, why, sizeof(why));
    CHECK(r == 16, "no device descriptor: R16");
    prt[1].Dev = devDesc;
    r = tr_rst_rules9x(&drv, nd, ndN, ctl9, 0, 0, prt, 2, 1, "X", why,
                       sizeof(why), NULL);
    CHECK(r == 16, "the port reading's bound met: R16");
    prt[1].Connected = 0;
    prt[1].Dev = NULL;
    prt[1].Cfg = NULL;
    r = rules(2, why, sizeof(why));
    CHECK(r == 0, "an empty port: nothing to read");
}

/* Item 3's sequence with a configuration manager of the vectors' own: each
 * call is logged with the handle it was given, so that a call through a
 * handle no call returned shows. */
typedef struct _FAKE9X {
    TR_U32 LocateCr;        /* the first locate of the controller          */
    TR_U32 AfterCr;         /* its locate once the removal returned         */
    TR_U32 RootCr;          /* the root's locate                            */
    TR_U32 ParentCr;
    TR_U32 QueryCr;
    TR_U32 RemoveCr;
    TR_U32 RescanCr[2];
    int Settles;
    int Locates;
    int Rescans;
    TR_RST_SEEN SeenAt[2];  /* what each settle sees                       */
    char Log[400];
} FAKE9X;

static void fk_log(FAKE9X *f, const char *s)
{
    strcat(f->Log, s);
}

static TR_U32 fk_locate(void *ctx, TR_U32 *dn, const char *id)
{
    FAKE9X *f = (FAKE9X *)ctx;
    TR_U32 cr;

    if (id == NULL) {
        fk_log(f, "Lroot ");
        cr = f->RootCr;
        *dn = cr == 0 ? 1 : 0;
        return cr;
    }
    cr = f->Locates++ == 0 ? f->LocateCr : f->AfterCr;
    fk_log(f, "Lid ");
    *dn = cr == 0 ? 100 : 0;
    return cr;
}

static TR_U32 fk_parent(void *ctx, TR_U32 *parent, TR_U32 dn)
{
    FAKE9X *f = (FAKE9X *)ctx;

    fk_log(f, dn == 100 ? "P(100) " : "P(?) ");
    *parent = f->ParentCr == 0 ? 50 : 0;
    return f->ParentCr;
}

static TR_U32 fk_query(void *ctx, TR_U32 dn)
{
    FAKE9X *f = (FAKE9X *)ctx;

    fk_log(f, dn == 100 ? "Q(100) " : "Q(?) ");
    return f->QueryCr;
}

static TR_U32 fk_remove(void *ctx, TR_U32 dn)
{
    FAKE9X *f = (FAKE9X *)ctx;

    fk_log(f, dn == 100 ? "R(100) " : "R(?) ");
    return f->RemoveCr;
}

static TR_U32 fk_rescan(void *ctx, TR_U32 dn)
{
    FAKE9X *f = (FAKE9X *)ctx;
    TR_U32 cr = f->RescanCr[f->Rescans < 2 ? f->Rescans : 1];

    f->Rescans++;
    fk_log(f, dn == 50 ? "E(50) " : dn == 1 ? "E(root) " : "E(?) ");
    return cr;
}

static void fk_settle(void *ctx, TR_RST_HIST *h, int gone, TR_RST_SEEN *s)
{
    FAKE9X *f = (FAKE9X *)ctx;
    const TR_RST_SEEN *at = &f->SeenAt[f->Settles < 2 ? f->Settles : 1];

    (void)h;
    fk_log(f, gone ? "S(gone) " : "S ");
    f->Settles++;
    s->Found |= at->Found;
    s->Seen |= at->Seen;
    s->Unsure |= at->Unsure;
}

static void seq(FAKE9X *f, TR_RST_IN *in, TR_RST_HIST *h)
{
    TR_RST_OPS9X ops;

    ops.Ctx = f;
    ops.Locate = fk_locate;
    ops.Parent = fk_parent;
    ops.QueryRemove = fk_query;
    ops.Remove = fk_remove;
    ops.Reenumerate = fk_rescan;
    ops.Settle = fk_settle;
    memset(in, 0, sizeof(*in));
    tr_rst_hist_init(h, NULL);
    tr_rst_seq9x(&ops, "PCI\\VEN_1B36&DEV_000D\\0", in, h);
}

static void fk_init(FAKE9X *f)
{
    memset(f, 0, sizeof(*f));
    f->AfterCr = 0x0000000DUL;      /* gone once removed */
    f->SeenAt[0].Found = 1;
    f->SeenAt[0].Seen = 1;
    f->SeenAt[1] = f->SeenAt[0];
}

static void test_seq9x(void)
{
    FAKE9X f;
    TR_RST_IN in;
    TR_RST_HIST h;

    fk_init(&f);
    seq(&f, &in, &h);
    CHECK(strcmp(f.Log, "Lid P(100) Q(100) R(100) Lid E(50) S(gone) ") == 0,
          "the whole sequence: parent before the query, the removal, the "
          "parent's rescan, one settle");
    CHECK(in.Request == TR_RST_REQ_9X_REMOVED && in.Rescans == 1 &&
          in.Start == TR_RST_S1, "A13, S1 by the removal");

    fk_init(&f);
    f.LocateCr = 0x0000000DUL;
    seq(&f, &in, &h);
    CHECK(strcmp(f.Log, "Lid ") == 0 && in.Request == TR_RST_REQ_NODEV,
          "the controller gone before: R10, nothing else called");
    fk_init(&f);
    f.LocateCr = 0x5;
    seq(&f, &in, &h);
    CHECK(strcmp(f.Log, "Lid ") == 0 && in.Request == TR_RST_REQ_9X_NOPARENT &&
          in.Error == 0x5, "the locate failing: A21, no call through its "
          "handle");
    fk_init(&f);
    f.ParentCr = 0x5;
    seq(&f, &in, &h);
    CHECK(strcmp(f.Log, "Lid P(100) ") == 0 &&
          in.Request == TR_RST_REQ_9X_NOPARENT,
          "CM_Get_Parent failing: A21, no query, no rescan");
    fk_init(&f);
    f.QueryCr = 0x17;
    seq(&f, &in, &h);
    CHECK(strcmp(f.Log, "Lid P(100) Q(100) ") == 0 &&
          in.Request == TR_RST_REQ_9X_VETO,
          "a vetoed query: A10, no removal");
    fk_init(&f);
    f.QueryCr = 0x1C;
    seq(&f, &in, &h);
    CHECK(strcmp(f.Log, "Lid P(100) Q(100) ") == 0 &&
          in.Request == TR_RST_REQ_9X_QFAIL && in.Error == 0x1C,
          "a query failing otherwise: A19, no removal");
    fk_init(&f);
    f.RemoveCr = 0x17;
    f.RescanCr[0] = 0x2;
    seq(&f, &in, &h);
    CHECK(strcmp(f.Log, "Lid P(100) Q(100) R(100) E(50) S ") == 0 &&
          in.Request == TR_RST_REQ_9X_RMFAIL && in.Rescans == 1 &&
          in.RescanCode[0] == 0x2,
          "a failed removal: A12, exactly one rescan of the parent, a "
          "settle, no root");
    CHECK(in.Start == TR_RST_S4, "... line 2 from the lifetime samples "
          "(none: S4)");

    fk_init(&f);
    f.RescanCr[0] = 0x1F;
    seq(&f, &in, &h);
    CHECK(strcmp(f.Log, "Lid P(100) Q(100) R(100) Lid E(50) S(gone) Lroot "
                 "E(root) S(gone) ") == 0 && in.Rescans == 2,
          "the parent's rescan failing: its settle, then the root's rescan "
          "and the settle again");
    fk_init(&f);
    f.SeenAt[0].Found = 0;
    f.SeenAt[0].Seen = 0;
    seq(&f, &in, &h);
    CHECK(strcmp(f.Log, "Lid P(100) Q(100) R(100) Lid E(50) S(gone) Lroot "
                 "E(root) S(gone) ") == 0 && in.Start == TR_RST_S1,
          "not found by the first settle: the root's rescan finds it");
    fk_init(&f);
    f.SeenAt[0].Found = 0;
    f.SeenAt[0].Seen = 0;
    f.RootCr = 0x5;
    seq(&f, &in, &h);
    CHECK(strcmp(f.Log, "Lid P(100) Q(100) R(100) Lid E(50) S(gone) Lroot ")
          == 0 && in.RootFail && in.RootCode == 0x5 && in.Rescans == 1,
          "the root not located: A22, no rescan through it");
    CHECK(in.Start == TR_RST_S_A11, "... and not found: A11");
    fk_init(&f);
    f.SeenAt[0].Found = 0;
    f.SeenAt[0].Seen = 0;
    f.SeenAt[0].Unsure = 1;
    f.SeenAt[1] = f.SeenAt[0];
    seq(&f, &in, &h);
    CHECK(in.Start == TR_RST_S4, "never located, a locate failing "
          "otherwise: S4, not A11");
    fk_init(&f);
    f.SeenAt[0].Seen = 0;
    f.SeenAt[1].Seen = 0;
    seq(&f, &in, &h);
    CHECK(in.Start == TR_RST_S2 && in.Rescans == 1,
          "found, never started: S2, no root rescan");
    fk_init(&f);
    f.AfterCr = 0;
    seq(&f, &in, &h);
    CHECK(strcmp(f.Log, "Lid P(100) Q(100) R(100) Lid E(50) S ") == 0 &&
          in.Start == TR_RST_S4,
          "the id still located after the removal: no evidence, the "
          "lifetime samples (none: S4)");
}

static void test_offered9x(void)
{
    CHECK(tr_rst_offered(1, 1, 2) && tr_rst_offered(1, 1, 3),
          "2000 and later: a stopped controller offered");
    CHECK(!tr_rst_offered(0, 1, 2) && !tr_rst_offered(0, 1, 3),
          "98 SE and ME: terminal 2 or 3 not offered");
    CHECK(tr_rst_offered(0, 1, 0) && tr_rst_offered(0, 1, 1) &&
          tr_rst_offered(0, 1, 4) && tr_rst_offered(0, 0, 0) &&
          tr_rst_offered(0, -1, 2),
          "98 SE and ME: running, a recovery owed, contained (refused by "
          "R2 instead), not served or unread (R8 where failed): offered");
}

/* Item 6's hub nodes as the window reads them (tr_rst_is_hubnode): this
 * driver's hubs carry the project-owned ids USB\XHCI98_HUB and
 * USB\XHCI98_HUB30, with no Class_09, on 98 SE and ME as on NT. */
static void test_hubnode(void)
{
    TR_RST_LIST l;
    char buf[1024];
    int root;
    int ctl;
    int rh;
    int hub;
    int rc;
    int i;
    static const char *svc[8];

    CHECK(tr_rst_is_hubnode("USB\\XHCI98_ROOT_HUB\\5&1&0&0", "", 0),
          "the root hub by its id");
    CHECK(tr_rst_is_hubnode("USB\\XHCI98_HUB&VID_0409&PID_55AA\\1-4",
                            "xhci98", 0),
          "a hub of this driver by its project-owned id, no Class_09");
    CHECK(tr_rst_is_hubnode("usb\\xhci98_hub30&vid_1&pid_2\\1", "XHCI98", 0),
          "a SuperSpeed hub's id and service, any case");
    CHECK(tr_rst_is_hubnode("USB\\VID_1&PID_2\\1", "xhci98", 1),
          "a Class_09 id with this driver");
    CHECK(!tr_rst_is_hubnode("USB\\VID_1&PID_2\\1", "usbhub", 1),
          "a hub of another driver: no");
    CHECK(!tr_rst_is_hubnode("USB\\XHCI98_HUB&VID_1&PID_2\\1", "", 0),
          "the hub id with no driver bound: no");
    CHECK(!tr_rst_is_hubnode("USB\\VID_0627&PID_0001\\1", "hidusb", 0),
          "a mouse: no");
    CHECK(!tr_rst_is_hubnode(NULL, NULL, 0), "nothing: no");

    /* The 98 SE reading: a hub of this driver at root port 8, a mouse
     * behind it; IsHubNode from the predicate, as the window sets it. */
    ndN = 0;
    root = node(-1, "HTREE\\ROOT\\0", "", "root", "");
    ctl = node(root, "PCI\\C", "USB", "ctl", "");
    rh = node(ctl, "USB\\XHCI98_ROOT_HUB\\1", "USB", "root hub", "");
    hub = node(rh, "USB\\XHCI98_HUB&VID_0409&PID_55AA\\1-4", "USB",
               "xHCI98 USB Hub", "port 8");
    (void)node(hub, "USB\\VID_0627&PID_0001\\1-4.2", "HID",
               "USB Human Interface Device", "port 2 of the hub at port 8");
    (void)node(ndN - 1, "HID\\VID_0627&PID_0001\\1", "Mouse",
               "HID-compliant mouse", "");
    svc[rh] = "xhci98";
    svc[hub] = "xhci98";
    for (i = 0; i < ndN; i++) {
        nd[i].IsHubNode = tr_rst_is_hubnode(nd[i].Id, svc[i], 0);
    }
    nd[rh].IsRootHub = 1;
    rc = list(ctl, 0, 0, &l, buf, sizeof(buf));
    CHECK(rc == TR_RST_LIST_OK &&
          strcmp(buf, "xHCI98 USB Hub, port 8\r\n"
                      "USB Human Interface Device, port 2 of the hub at "
                      "port 8 - mouse\r\n") == 0,
          "the device behind this driver's hub on a line of its own, the hub's "
          "line without it");
    if (rc == TR_RST_LIST_OK && strstr(buf, "port 2 of") == NULL) {
        printf("%s\n", buf);
    }
}

int main(void)
{
    test_hist();
    test_args();
    test_answer();
    test_line4();
    test_list();
    test_child();
    test_digest();
    test_check();
    test_texts();
    test_carry();
    test_start9x();
    test_answer9x();
    test_hint();
    test_storage9x();
    test_rules9x();
    test_seq9x();
    test_offered9x();
    test_hubnode();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
