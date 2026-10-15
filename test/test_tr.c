/*
 * test_tr.c - host vectors for XHCITR.EXE's pure half (xhcitr\tr_core.c;
 * design record 25 section 10; roadmap-usb4.md task 41-A.2): the bounded
 * printf, the USB IOCTL answers read as bytes, the descriptor decoders
 * over well-formed, truncated and malformed input, xhcicm.sys's query
 * answer and its refusals, the status record's texts, the tray's event
 * diff, the tree's keys, the command line, the report's layout and the
 * packed request sizes.
 *
 * cm_live.h is not included: it needs the driver's compatibility header.
 * tr_live_text is checked for every state against the texts instead.
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "../xhcitr/tr_core.h"
#include "../src/cm/cm_status.h"
#include "../src/cm/cm_query.h"
#include "test_harness.h"

#define QB_ID "PCI\\VEN_8086&DEV_15EB&SUBSYS_00000000&REV_06\\4&2B3C0E5&0&00E4"
#define QB_EPOCH_HI 0x01D9ABCDUL

static unsigned char qa[2048];
static unsigned char qb[2048];
static char big[16384];

static void put16(unsigned char *b, size_t o, unsigned int v)
{
    b[o] = (unsigned char)(v & 0xFFU);
    b[o + 1] = (unsigned char)((v >> 8) & 0xFFU);
}

static void put32(unsigned char *b, size_t o, TR_U32 v)
{
    b[o] = (unsigned char)(v & 0xFFUL);
    b[o + 1] = (unsigned char)((v >> 8) & 0xFFUL);
    b[o + 2] = (unsigned char)((v >> 16) & 0xFFUL);
    b[o + 3] = (unsigned char)((v >> 24) & 0xFFUL);
}

static int has(const char *hay, const char *needle)
{
    return strstr(hay, needle) != NULL;
}

/* ---- text ---------------------------------------------------------------- */

static void test_printf(void)
{
    char buf[128];
    char small[4];
    TR_TEXT t;

    tr_sprintf(buf, sizeof(buf), "%s|%d|%u|%lu|%x|%04X|%-10s|%08lX",
               "ab", -42, 7U, 4000000000UL, 255U, 0xABU, "left", 0x1234ABUL);
    CHECK(strcmp(buf, "ab|-42|7|4000000000|ff|00AB|left      |001234AB") == 0,
          "printf: every conversion");
    tr_sprintf(buf, sizeof(buf), "[%5d][%05d][%-4d][%ld]", -3, -3, 12,
               -2147483647L - 1L);
    CHECK(strcmp(buf, "[   -3][-0003][12  ][-2147483648]") == 0,
          "printf: negative widths and the most negative long");
    tr_sprintf(buf, sizeof(buf), "%c%%%6s|%d", 'Z', "r", 0);
    CHECK(strcmp(buf, "Z%     r|0") == 0, "printf: %c, %%, right-aligned %s");
    tr_sprintf(buf, sizeof(buf), "%s", (const char *)NULL);
    CHECK(strcmp(buf, "(null)") == 0, "printf: a NULL string");
    tr_sprintf(buf, sizeof(buf), "a\nb\n");
    CHECK(strcmp(buf, "a\r\nb\r\n") == 0, "printf: LF becomes CR LF");

    tr_text_init(&t, buf, 8);
    tr_put(&t, "0123456789");
    CHECK(t.Overflow == 1, "put past the end sets Overflow");
    CHECK_EQ(t.Len, 7, "put past the end keeps cap - 1");
    CHECK(strcmp(buf, "0123456") == 0, "put past the end stays terminated");

    tr_text_init(&t, buf, 4);
    tr_put(&t, "ab\n");
    CHECK(t.Overflow == 1, "a CR LF that does not fit sets Overflow");
    CHECK(strcmp(buf, "ab") == 0, "a CR LF is never cut in half");

    tr_sprintf(small, sizeof(small), "%s", "hello");
    CHECK(strcmp(small, "hel") == 0, "sprintf is bounded and terminated");
    tr_sprintf(small, sizeof(small), "%08lX", 0xBEEFUL);
    CHECK(strcmp(small, "000") == 0, "sprintf bounds a padded number");
    tr_sprintf(small, sizeof(small), "%lX", 0xDEADBEEFUL);
    CHECK(strcmp(small, "DEA") == 0, "sprintf bounds a number");

    tr_text_init(&t, buf, sizeof(buf));
    tr_printf(&t, "x=%u", 5U);
    tr_printf(&t, ", y=%s\n", "z");
    CHECK(strcmp(buf, "x=5, y=z\r\n") == 0, "printf appends");
    CHECK_EQ(t.Len, 10, "printf's length");
    CHECK(t.Overflow == 0, "no overflow");
}

static void test_hex_names(void)
{
    unsigned char b[40];
    char out[64];
    TR_TEXT t;
    int i;

    for (i = 0; i < 17; i++) {
        b[i] = (unsigned char)(i * 0x11);
    }
    tr_text_init(&t, big, sizeof(big));
    tr_hex(&t, b, 17, 2);
    CHECK(strcmp(big, "  0000: 00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF\r\n"
                      "  0010: 10\r\n") == 0, "hex: 16 to a line, indented");
    tr_text_init(&t, big, sizeof(big));
    tr_hex(&t, b, 0, 4);
    CHECK(big[0] == '\0', "hex of nothing is nothing");

    memset(b, 0, sizeof(b));
    put32(b, 0, 99UL);
    put16(b, 4, 'H');
    put16(b, 6, 'u');
    put16(b, 8, 'b');
    CHECK(tr_wide_name(b, 12, 4, out, sizeof(out)) == 1, "wide name read");
    CHECK(strcmp(out, "Hub") == 0, "wide name: ASCII");
    put16(b, 6, 0x00E9U);
    CHECK(tr_wide_name(b, 12, 4, out, sizeof(out)) == 1, "wide name, non-ASCII");
    CHECK(strcmp(out, "H?b") == 0, "wide name: non-ASCII becomes '?'");
    put16(b, 6, 'u');
    CHECK(tr_wide_name(b, 9, 4, out, sizeof(out)) == 1, "wide name, odd length");
    CHECK(strcmp(out, "Hu") == 0, "wide name: an odd last byte is dropped");
    CHECK(tr_wide_name(b, 10, 10, out, sizeof(out)) == 0, "wide name: empty");
    CHECK(out[0] == '\0', "wide name: empty is terminated");
    CHECK(tr_wide_name(b, 12, 4, out, 3) == 1, "wide name: bounded");
    CHECK(strcmp(out, "Hu") == 0, "wide name: bounded and terminated");

    CHECK(tr_eq("Tray", "TRAY"), "eq without case");
    CHECK(!tr_eq("tray", "tra"), "eq: a prefix is not equal");
    CHECK(!tr_eq("tra", "tray"), "eq: a longer one is not equal");
    CHECK(tr_eq("", ""), "eq: two empty");
    CHECK(!tr_eq("a[", "A{"), "eq folds letters only");

    tr_copy(out, "abcdef", 4);
    CHECK(strcmp(out, "abc") == 0, "copy is bounded");
}

/* ---- the USB answers ----------------------------------------------------- */

static void test_conn(void)
{
    unsigned char b[80];
    TR_CONN c;
    unsigned int ports;
    int bp;
    int i;

    memset(b, 0, sizeof(b));
    put32(b, 0, 3UL);
    for (i = 0; i < 18; i++) {
        b[4 + i] = (unsigned char)(0xA0 + i);
    }
    b[22] = 1;
    b[23] = 2;
    b[24] = 1;
    put16(b, 25, 0x0105U);
    put32(b, 27, 2UL);
    put32(b, 31, TR_CS_CONNECTED);
    b[35] = 7;
    b[36] = 5;
    b[37] = 0x81;
    b[38] = 3;
    b[46] = 7;
    b[47] = 5;
    b[48] = 0x02;
    b[49] = 2;

    CHECK(tr_conn_parse(b, 35 + 22, 1, &c) == 1, "_EX answer parsed");
    CHECK_EQ(c.Port, 3, "port");
    CHECK(c.Dev[0] == 0xA0 && c.Dev[17] == 0xB1, "device descriptor copied");
    CHECK_EQ(c.Config, 1, "configuration value");
    CHECK_EQ(c.Speed, TR_SPEED_HIGH, "_EX speed");
    CHECK(c.IsHub == 1, "is a hub");
    CHECK_EQ(c.Address, 0x0105, "address");
    CHECK_EQ(c.OpenPipes, 2, "open pipes");
    CHECK_EQ(c.Status, TR_CS_CONNECTED, "status");
    CHECK_EQ(c.PipeCount, 2, "both pipes held");
    CHECK(c.Pipe[0][2] == 0x81 && c.Pipe[1][2] == 0x02, "pipes' endpoints");

    CHECK(tr_conn_parse(b, 35 + 11 + 5, 1, &c) == 1, "pipes cut by n");
    CHECK_EQ(c.PipeCount, 1, "a pipe past n is not read");
    CHECK(tr_conn_parse(b, 35, 1, &c) == 1, "no room for pipes");
    CHECK_EQ(c.PipeCount, 0, "no pipe read");
    CHECK(tr_conn_parse(b, 34, 1, &c) == 0, "short buffer refused");

    b[23] = 1;
    CHECK(tr_conn_parse(b, 35, 0, &c) == 1, "non-EX answer parsed");
    CHECK_EQ(c.Speed, TR_SPEED_LOW, "LowSpeed 1 is Low Speed");
    b[23] = 0;
    CHECK(tr_conn_parse(b, 35, 0, &c) == 1, "non-EX answer parsed, 0");
    CHECK_EQ(c.Speed, TR_SPEED_UNKNOWN, "LowSpeed 0 is not known");

    CHECK(strcmp(tr_conn_status_text(TR_CS_CONNECTED), "device connected") == 0,
          "status text 1");
    CHECK(strcmp(tr_conn_status_text(0UL), "no device connected") == 0,
          "status text 0");
    CHECK(strcmp(tr_conn_status_text(99UL), "status not known") == 0,
          "status text unknown");

    CHECK_EQ(tr_speed(TR_SPEED_HIGH, 0, 0UL), TR_SPEED_HIGH, "speed _EX");
    CHECK_EQ(tr_speed(TR_SPEED_SUPER, 0, 0UL), TR_SPEED_SUPER, "speed Super");
    CHECK_EQ(tr_speed(TR_SPEED_SUPER, 1, TR_V2_F_SSP_NOW | TR_V2_F_SS_NOW),
             TR_SPEED_SUPERPLUS, "V2 SuperSpeedPlus now");
    CHECK_EQ(tr_speed(TR_SPEED_HIGH, 1, TR_V2_F_SS_NOW), TR_SPEED_SUPER,
             "V2 SuperSpeed now");
    CHECK_EQ(tr_speed(TR_SPEED_HIGH, 1, TR_V2_F_SSP_CAPABLE | TR_V2_F_SS_CAPABLE),
             TR_SPEED_HIGH, "V2 capable only keeps the _EX speed");
    CHECK_EQ(tr_speed(TR_SPEED_HIGH, 0, TR_V2_F_SSP_NOW), TR_SPEED_HIGH,
             "flags without V2 ignored");
    CHECK(tr_speed(TR_SPEED_UNKNOWN, 0, 0UL) == -1, "unknown speed is -1");
    CHECK(strcmp(tr_speed_name(-1), "speed not reported") == 0, "speed -1 name");
    CHECK(strcmp(tr_speed_name(TR_SPEED_HIGH), "High Speed (480 Mbit/s)") == 0,
          "High Speed name");
    CHECK(strcmp(tr_speed_name(TR_SPEED_SUPERPLUS), "SuperSpeedPlus") == 0,
          "SuperSpeedPlus name");

    memset(b, 0, sizeof(b));
    b[4] = 9;
    b[5] = 0x29;
    b[6] = 4;
    b[75] = 1;
    ports = 0;
    bp = 0;
    CHECK(tr_node_parse(b, 76, &ports, &bp) == 1, "node: a hub");
    CHECK_EQ(ports, 4, "node: ports");
    CHECK(bp == 1, "node: bus powered");
    b[75] = 0;
    CHECK(tr_node_parse(b, 76, &ports, &bp) == 1 && bp == 0, "node: self powered");
    CHECK(tr_node_parse(b, 75, &ports, &bp) == 0, "node: short refused");
    put32(b, 0, 1UL);
    CHECK(tr_node_parse(b, 76, &ports, &bp) == 0, "node: not a hub refused");
}

/* ---- descriptors ----------------------------------------------------------- */

static const unsigned char devDesc[18] = {
    0x12, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x5E, 0x04, 0x8E, 0x02,
    0x00, 0x01, 0x01, 0x02, 0x03, 0x01
};

static const unsigned char cfgDesc[48] = {
    /* configuration: 48 bytes, 2 interfaces, bus-powered, remote wakeup */
    0x09, 0x02, 0x30, 0x00, 0x02, 0x01, 0x00, 0xA0, 0x32,
    /* IAD: interfaces 0..1, Video */
    0x08, 0x0B, 0x00, 0x02, 0x0E, 0x03, 0x00, 0x00,
    /* interface 0: HID, one endpoint */
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x02, 0x00,
    /* HID 1.11, report descriptor 63 bytes */
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x3F, 0x00,
    /* endpoint IN 1, interrupt, 8 bytes, interval 10 */
    0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x0A,
    /* SuperSpeed companion */
    0x06, 0x30, 0x00, 0x00, 0x08, 0x00
};

static void test_device_config(void)
{
    unsigned char d[64];
    TR_TEXT t;
    unsigned int ma;
    int sp;

    tr_text_init(&t, big, sizeof(big));
    tr_dec_device(&t, devDesc, sizeof(devDesc));
    CHECK(has(big, "Device descriptor\r\n"), "device: title");
    CHECK(has(big, "  bcdUSB               2.00\r\n"), "device: bcdUSB");
    CHECK(has(big, "bDeviceClass         0x00 (defined per interface)"),
          "device: class");
    CHECK(has(big, "bMaxPacketSize0      64"), "device: packet size");
    CHECK(has(big, "idVendor             0x045E"), "device: vendor");
    CHECK(has(big, "idProduct            0x028E"), "device: product");
    CHECK(has(big, "bcdDevice            1.00"), "device: bcdDevice");
    CHECK(has(big, "iSerialNumber        3"), "device: serial index");
    CHECK(has(big, "  0000: 12 01 00 02"), "device: bytes listed");
    CHECK(has(big, "  0010: 03 01\r\n"), "device: last bytes listed");
    CHECK(t.Overflow == 0, "device: no overflow");

    tr_text_init(&t, big, sizeof(big));
    tr_dec_device(&t, devDesc, 10);
    CHECK(has(big, "(short or not a device descriptor)"), "device: short");
    CHECK(has(big, "  0000: 12 01 00 02 00 00 00 40 5E 04\r\n"),
          "device: short still listed");

    tr_text_init(&t, big, sizeof(big));
    tr_dec_config(&t, cfgDesc, sizeof(cfgDesc));
    CHECK(has(big, "Configuration descriptor\r\n"), "config: title");
    CHECK(has(big, "wTotalLength         48"), "config: total");
    CHECK(has(big, "bNumInterfaces       2"), "config: interfaces");
    CHECK(has(big, "0xA0 (bus-powered, remote wakeup)"), "config: attributes");
    CHECK(has(big, "bMaxPower            50 (units"), "config: power");
    CHECK(has(big, "Interface association descriptor"), "config: IAD");
    CHECK(has(big, "bFunctionClass       0x0E (Video)"), "config: IAD class");
    CHECK(has(big, "Interface descriptor"), "config: interface");
    CHECK(has(big, "bInterfaceClass      0x03 (Human Interface Device)"),
          "config: interface class");
    CHECK(has(big, "HID descriptor"), "config: HID");
    CHECK(has(big, "bcdHID               1.11"), "config: bcdHID");
    CHECK(has(big, "report descriptor    0x22, 63 bytes"), "config: report");
    CHECK(has(big, "Endpoint descriptor"), "config: endpoint");
    CHECK(has(big, "bEndpointAddress     0x81 (IN 1)"), "config: endpoint address");
    CHECK(has(big, "0x03 (Interrupt)"), "config: endpoint type");
    CHECK(has(big, "wMaxPacketSize       0x0008 (8 bytes)"), "config: packet");
    CHECK(has(big, "SuperSpeed endpoint companion descriptor"),
          "config: SS companion");
    CHECK(has(big, "wBytesPerInterval    8"), "config: bytes per interval");
    CHECK(!has(big, "Malformed"), "config: nothing malformed");
    CHECK(!has(big, "only"), "config: not truncated");
    CHECK(has(big, "  0000: 06 30 00 00 08 00\r\n"), "config: last bytes listed");

    tr_text_init(&t, big, sizeof(big));
    tr_dec_config(&t, cfgDesc, 30);
    CHECK(has(big, "(wTotalLength 48, only 30 bytes read)"), "config: truncated");
    CHECK(has(big, "Interface descriptor"), "config: truncated still decodes");
    CHECK(has(big, "Malformed descriptor at offset 26 (length 9)"),
          "config: truncated at the HID descriptor");
    CHECK(!has(big, "Endpoint descriptor"), "config: nothing past the cut");

    memcpy(d, cfgDesc, 9);
    d[2] = 11;
    d[9] = 0;
    d[10] = 5;
    tr_text_init(&t, big, sizeof(big));
    tr_dec_config(&t, d, 11);
    CHECK(has(big, "Malformed descriptor at offset 9 (length 0)"),
          "config: length 0");

    memcpy(d, cfgDesc, 9);
    d[2] = 20;
    d[9] = 0x20;
    d[10] = 0x04;
    tr_text_init(&t, big, sizeof(big));
    tr_dec_config(&t, d, 20);
    CHECK(has(big, "Malformed descriptor at offset 9 (length 32)"),
          "config: length past the end");

    memcpy(d, cfgDesc, 9);
    d[2] = 13;
    d[9] = 4;
    d[10] = 0x05;
    d[11] = 0x81;
    d[12] = 0x03;
    tr_text_init(&t, big, sizeof(big));
    tr_dec_config(&t, d, 13);
    CHECK(has(big, "Descriptor of type 0x05, too short (4 bytes)"),
          "config: a short endpoint");

    tr_text_init(&t, big, sizeof(big));
    tr_dec_config(&t, cfgDesc, 5);
    CHECK(has(big, "Configuration descriptor: short or malformed"),
          "config: short");
    tr_text_init(&t, big, sizeof(big));
    tr_dec_config(&t, devDesc, 18);
    CHECK(has(big, "Configuration descriptor: short or malformed"),
          "config: not a configuration");

    CHECK(tr_config_power(cfgDesc, 9, TR_SPEED_HIGH, &ma, &sp) == 1, "power read");
    CHECK_EQ(ma, 100, "power: 2 mA units at High Speed");
    CHECK(sp == 0, "power: bus-powered");
    CHECK(tr_config_power(cfgDesc, 9, TR_SPEED_SUPER, &ma, &sp) == 1,
          "power read at SuperSpeed");
    CHECK_EQ(ma, 400, "power: 8 mA units at SuperSpeed");
    CHECK(tr_config_power(cfgDesc, 9, TR_SPEED_SUPERPLUS, &ma, &sp) == 1 &&
          ma == 400, "power: 8 mA units at SuperSpeedPlus");
    memcpy(d, cfgDesc, 9);
    d[7] = 0xC0;
    CHECK(tr_config_power(d, 9, TR_SPEED_FULL, &ma, &sp) == 1 && sp == 1,
          "power: self-powered");
    CHECK(tr_config_power(d, 8, TR_SPEED_FULL, &ma, &sp) == 0, "power: short");
    CHECK(tr_config_power(devDesc, 18, TR_SPEED_FULL, &ma, &sp) == 0,
          "power: not a configuration");

    CHECK(strcmp(tr_class_name(0x08), "Mass Storage") == 0, "class name");
    CHECK(strcmp(tr_class_name(0x42), "class not known") == 0, "class unknown");
}

static const unsigned char bosDesc[42] = {
    /* BOS: 42 bytes, 3 capabilities */
    0x05, 0x0F, 0x2A, 0x00, 0x03,
    /* USB 2.0 extension: LPM */
    0x07, 0x10, 0x02, 0x02, 0x00, 0x00, 0x00,
    /* SuperSpeed: full, high, 5 Gbit/s */
    0x0A, 0x10, 0x03, 0x00, 0x0E, 0x00, 0x01, 0x0A, 0xFF, 0x07,
    /* SuperSpeedPlus: two attributes, one rx and one tx lane */
    0x14, 0x10, 0x0A, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00,
    0x30, 0x40, 0x0A, 0x00,
    0xB0, 0x40, 0x0A, 0x00
};

static void test_bos_hub_string(void)
{
    unsigned char d[64];
    char out[96];
    TR_TEXT t;

    tr_text_init(&t, big, sizeof(big));
    tr_dec_bos(&t, bosDesc, sizeof(bosDesc));
    CHECK(has(big, "BOS descriptor\r\n"), "BOS: title");
    CHECK(has(big, "wTotalLength         42"), "BOS: total");
    CHECK(has(big, "bNumDeviceCaps       3"), "BOS: caps");
    CHECK(has(big, "USB 2.0 extension capability"), "BOS: USB 2.0 ext");
    CHECK(has(big, "0x00000002 (LPM)"), "BOS: LPM");
    CHECK(has(big, "SuperSpeed capability"), "BOS: SuperSpeed");
    CHECK(has(big, "0x000E (full high 5 Gbit/s)"), "BOS: speeds");
    CHECK(has(big, "bU1DevExitLat        10 us"), "BOS: U1");
    CHECK(has(big, "wU2DevExitLat        2047 us"), "BOS: U2");
    CHECK(has(big, "SuperSpeedPlus capability"), "BOS: SSP");
    CHECK(has(big, "(2 sublink speed attributes)"), "BOS: SSP count");
    CHECK(has(big, "(minimum id 0, rx lanes 1, tx lanes 1)"), "BOS: SSP lanes");
    CHECK(has(big, "sublink speed id 0: 10 Gbit/s, symmetric, SuperSpeedPlus"),
          "BOS: SSP rx attribute");
    CHECK(has(big, "sublink speed id 0: 10 Gbit/s, symmetric (transmit), "
                   "SuperSpeedPlus"), "BOS: SSP tx attribute");
    CHECK(!has(big, "Malformed"), "BOS: nothing malformed");
    CHECK(!has(big, "cut short"), "BOS: attributes whole");

    CHECK(tr_bos_ssp_text(bosDesc, sizeof(bosDesc), out, sizeof(out)) == 1,
          "SSP text found");
    CHECK(strcmp(out, "10 Gbit/s per lane") == 0, "SSP text: tx half skipped");

    memcpy(d, bosDesc, sizeof(bosDesc));
    d[26] = 0x02;                       /* three attributes, two present */
    tr_text_init(&t, big, sizeof(big));
    tr_dec_bos(&t, d, sizeof(bosDesc));
    CHECK(has(big, "(3 sublink speed attributes)"), "BOS: claimed count");
    CHECK(has(big, "(attributes cut short)"), "BOS: attributes cut short");

    memcpy(d, bosDesc, sizeof(bosDesc));
    d[38] = 0x31;                       /* id 1, 5 Gbit/s, receive */
    d[39] = 0x00;
    d[40] = 0x05;
    CHECK(tr_bos_ssp_text(d, sizeof(bosDesc), out, sizeof(out)) == 1,
          "SSP text, two speeds");
    CHECK(strcmp(out, "10 Gbit/s, 5 Gbit/s per lane") == 0,
          "SSP text: both receive speeds");

    memcpy(d, bosDesc, 22);
    d[2] = 22;
    d[4] = 2;
    CHECK(tr_bos_ssp_text(d, 22, out, sizeof(out)) == 0, "no SSP capability");
    CHECK(out[0] == '\0', "no SSP: empty text");

    memcpy(d, bosDesc, 12);
    d[2] = 15;
    d[12] = 0;
    d[13] = 0x10;
    d[14] = 0x03;
    tr_text_init(&t, big, sizeof(big));
    tr_dec_bos(&t, d, 15);
    CHECK(has(big, "Malformed capability at offset 12 (length 0)"),
          "BOS: length 0");
    CHECK(has(big, "USB 2.0 extension capability"), "BOS: before the fault");

    memcpy(d, bosDesc, sizeof(bosDesc));
    tr_text_init(&t, big, sizeof(big));
    tr_dec_bos(&t, d, 30);
    CHECK(has(big, "(only 30 bytes read)"), "BOS: truncated");
    CHECK(has(big, "Malformed capability at offset 22 (length 20)"),
          "BOS: truncated SSP");
    CHECK(tr_bos_ssp_text(d, 30, out, sizeof(out)) == 0, "SSP text: truncated");

    tr_text_init(&t, big, sizeof(big));
    tr_dec_bos(&t, bosDesc, 3);
    CHECK(has(big, "BOS descriptor: short or malformed"), "BOS: short");
    CHECK(tr_bos_ssp_text(bosDesc, 3, out, sizeof(out)) == 0, "SSP text: short");
    tr_text_init(&t, big, sizeof(big));
    tr_dec_bos(&t, devDesc, 18);
    CHECK(has(big, "BOS descriptor: short or malformed"), "BOS: wrong type");

    memset(d, 0, sizeof(d));
    d[0] = 9;
    d[1] = 0x29;
    d[2] = 4;
    put16(d, 3, 0x00E9U);
    d[5] = 0x32;
    d[6] = 0x64;
    d[7] = 0x00;
    d[8] = 0xFF;
    tr_text_init(&t, big, sizeof(big));
    tr_dec_hub(&t, d, 71);
    CHECK(has(big, "USB 2.0 hub descriptor"), "hub 2.0: title");
    CHECK(has(big, "bNbrPorts            4"), "hub 2.0: ports");
    CHECK(has(big, "wHubCharacteristics  0x00E9"), "hub 2.0: characteristics");
    CHECK(has(big, "power switching: per port"), "hub 2.0: power switching");
    CHECK(has(big, "compound device: no"), "hub 2.0: compound");
    CHECK(has(big, "over-current protection: per port"), "hub 2.0: over-current");
    CHECK(has(big, "port indicators: yes"), "hub 2.0: indicators");
    CHECK(has(big, "bPwrOn2PwrGood       100 ms"), "hub 2.0: power-on");
    CHECK(has(big, "bHubContrCurrent     100 mA"), "hub 2.0: current");
    CHECK(has(big, "  0000: 09 29 04 E9 00 32 64 00 FF\r\n"),
          "hub 2.0: bLength bytes listed");

    memset(d, 0, sizeof(d));
    d[0] = 12;
    d[1] = 0x2A;
    d[2] = 2;
    put16(d, 3, 0x0016U);
    d[5] = 0x0A;
    d[6] = 0;
    tr_text_init(&t, big, sizeof(big));
    tr_dec_hub(&t, d, 12);
    CHECK(has(big, "SuperSpeed hub descriptor"), "hub 3: title");
    CHECK(has(big, "power switching: none"), "hub 3: no power switching");
    CHECK(has(big, "compound device: yes"), "hub 3: compound");
    CHECK(has(big, "over-current protection: none"), "hub 3: no over-current");
    CHECK(!has(big, "port indicators"), "hub 3: no indicators line");
    CHECK(has(big, "bPwrOn2PwrGood       20 ms"), "hub 3: power-on");

    tr_text_init(&t, big, sizeof(big));
    tr_dec_hub(&t, d, 6);
    CHECK(has(big, "Hub descriptor: short or not reported"), "hub: short");
    d[1] = 0x01;
    tr_text_init(&t, big, sizeof(big));
    tr_dec_hub(&t, d, 12);
    CHECK(has(big, "Hub descriptor: short or not reported"), "hub: wrong type");

    memset(d, 0, sizeof(d));
    d[0] = 10;
    d[1] = 0x03;
    put16(d, 2, 'H');
    put16(d, 4, 'i');
    put16(d, 6, 0x00E9U);
    put16(d, 8, '!');
    put16(d, 10, 'X');
    CHECK(tr_dec_string(d, 12, out, sizeof(out)) == 1, "string valid");
    CHECK(strcmp(out, "Hi?!") == 0, "string: bLength bounds it");
    CHECK(tr_dec_string(d, 6, out, sizeof(out)) == 1, "string cut by n");
    CHECK(strcmp(out, "Hi") == 0, "string: n bounds it");
    d[1] = 0x02;
    CHECK(tr_dec_string(d, 12, out, sizeof(out)) == 0, "string: wrong type");
    CHECK(out[0] == '\0', "string: wrong type is empty");
    d[1] = 0x03;
    CHECK(tr_dec_string(d, 1, out, sizeof(out)) == 0, "string: short");
    d[0] = 2;
    CHECK(tr_dec_string(d, 12, out, sizeof(out)) == 1, "string: empty is valid");
    CHECK(out[0] == '\0', "string: empty");
}

/* ---- xhcicm.sys's query -------------------------------------------------- */

static size_t qb_eoff(TR_U32 i)
{
    return (size_t)(CM_Q_HDR_BYTES + CM_ST_HDR_BYTES + i * CM_ST_ENTRY_BYTES);
}

/* A whole, valid answer with `routers` zeroed entries; its size. */
static size_t qb_build(unsigned char *b, TR_U32 routers, TR_U32 epochLo,
                       TR_U32 pass)
{
    TR_U32 recBytes = CM_ST_HDR_BYTES + routers * CM_ST_ENTRY_BYTES;
    TR_U32 extOff = CM_Q_HDR_BYTES + recBytes;
    TR_U32 idOff = extOff + routers * CM_Q_EXT_BYTES;
    TR_U32 idLen = (TR_U32)strlen(QB_ID);
    TR_U32 bytes = idOff + idLen;
    unsigned char *r = b + CM_Q_HDR_BYTES;
    TR_U32 i;

    memset(b, 0, (size_t)bytes + 64);
    put32(b, CM_Q_O_MAGIC, CM_Q_MAGIC);
    put32(b, CM_Q_O_VERSION, CM_Q_VERSION);
    put32(b, CM_Q_O_BYTES, bytes);
    put32(b, CM_Q_O_PASS, pass);
    put32(b, CM_Q_O_PASS_AGE_MS, 100UL);
    put32(b, CM_Q_O_LIVE, 1UL);
    put32(b, CM_Q_O_FLAGS, 0UL);
    put32(b, CM_Q_O_REC_OFFSET, CM_Q_HDR_BYTES);
    put32(b, CM_Q_O_EXT_OFFSET, extOff);
    put32(b, CM_Q_O_EXT_SHAPE, routers | (CM_Q_EXT_BYTES << 16));
    put32(b, CM_Q_O_EPOCH_LO, epochLo);
    put32(b, CM_Q_O_EPOCH_HI, QB_EPOCH_HI);
    put32(b, CM_Q_O_NHI_BDF, 0x0300UL);
    put32(b, CM_Q_O_ID_OFFSET, idOff);
    put32(b, CM_Q_O_ID_BYTES, idLen);
    put32(b, CM_Q_O_CONTENT_GEN, 3UL);

    put32(r, CM_ST_O_MAGIC, CM_ST_MAGIC);
    put32(r, CM_ST_O_VERSION, CM_ST_VERSION);
    put32(r, CM_ST_O_BYTES, recBytes);
    put32(r, CM_ST_O_GENERATION, 7UL);
    put32(r, CM_ST_O_UPTIME_S, 60UL);
    put32(r, CM_ST_O_NHI_ID, 0x15EB8086UL);
    put32(r, CM_ST_O_CM, CM_ST_CM_USB4);
    put32(r, CM_ST_O_STAGE, CM_ST_STAGE_ACTIVE);
    put32(r, CM_ST_O_SECURITY, CM_ST_UNKNOWN);
    put32(r, CM_ST_O_FW_VERSION, CM_ST_UNKNOWN);
    put32(r, CM_ST_O_PORT_COUNT, 1UL);
    put32(r, CM_ST_O_PORT_ACTIVE, 1UL);
    put32(r, CM_ST_O_PORTS, 0x0400UL);
    put32(r, CM_ST_O_ROUTER_COUNT, routers);
    put32(r, CM_ST_O_ENTRY_BYTES, CM_ST_ENTRY_BYTES);

    for (i = 0; i < routers; i++) {
        unsigned char *x = b + extOff + i * CM_Q_EXT_BYTES;
        put32(x, CM_Q_X_RATE, 20UL);
        put32(x, CM_Q_X_WIDTH, 2UL);
        put32(x, CM_Q_X_LINK_STATE, 7UL + i);
        put32(x, CM_Q_X_GEN_RAW, 3UL);
        put32(x, CM_Q_X_IDS, 0x00011234UL + i);
        put32(x, CM_Q_X_FLAGS, CM_Q_XF_RATE_READ);
    }
    memcpy(b + idOff, QB_ID, (size_t)idLen);
    return (size_t)bytes;
}

static void qb_entry(unsigned char *b, TR_U32 i, TR_U32 kind, TR_U32 place,
                     TR_U32 routeLo, TR_U32 approval, TR_U32 tunnels,
                     TR_U32 gen, const char *vendor, const char *model)
{
    unsigned char *e = b + qb_eoff(i);

    memset(e, 0, (size_t)CM_ST_ENTRY_BYTES);
    put32(e, CM_ST_E_KIND, kind);
    put32(e, CM_ST_E_PLACE, place);
    put32(e, CM_ST_E_ROUTE_LO, routeLo);
    put32(e, CM_ST_E_ROUTE_HI, 0UL);
    put32(e, CM_ST_E_APPROVAL, approval);
    put32(e, CM_ST_E_IDS, kind == CM_ST_KIND_ICM ? 0x12348086UL : 0x15EF8086UL);
    put32(e, CM_ST_E_GEN, gen);
    put32(e, CM_ST_E_LANES, CM_ST_LANES_BONDED);
    put32(e, CM_ST_E_TUNNELS, tunnels);
    put32(e, CM_ST_E_BRIDGE, 0x0500UL);
    put32(e, CM_ST_E_SHORT_ID, kind == CM_ST_KIND_ICM ? 0xABCDUL : CM_ST_UNKNOWN);
    memcpy(e + CM_ST_E_VENDOR, vendor, strlen(vendor));
    memcpy(e + CM_ST_E_MODEL, model, strlen(model));
}

static void test_query(void)
{
    size_t n;
    size_t n2;
    TR_QUERY q;
    TR_QUERY q2;
    TR_EXT x;
    const char *why;
    TR_U32 extOff;
    TR_U32 idOff;

    n = qb_build(qa, 2UL, 0x11111111UL, 5UL);
    qb_entry(qa, 0UL, CM_ST_KIND_USB4_PORT, 0x0101UL, 1UL, CM_ST_APPR_APPROVED,
             CM_ST_TUN_UP, CM_ST_GEN_USB4 | 0x1000UL, "Acme", "Dock");
    qb_entry(qa, 1UL, CM_ST_KIND_ICM, 0x0102UL, 0UL, CM_ST_APPR_APPROVED,
             CM_ST_TUN_UP, CM_ST_GEN_TBT3, "Apple", "Display");
    CHECK_EQ(n, 64 + 128 + 256 + 64 + strlen(QB_ID), "the built answer's size");
    extOff = 64UL + 128UL + 256UL;
    idOff = extOff + 64UL;

    CHECK(tr_query_parse(qa, n, &q) == 1, "a whole answer parses");
    CHECK(q.Base == qa, "Base");
    CHECK_EQ(q.Bytes, n, "Bytes");
    CHECK_EQ(q.Pass, 5, "Pass");
    CHECK_EQ(q.AgeMs, 100, "AgeMs");
    CHECK_EQ(q.Live, 1, "Live");
    CHECK_EQ(q.Flags, 0, "Flags");
    CHECK_EQ(q.EpochLo, 0x11111111UL, "EpochLo");
    CHECK_EQ(q.EpochHi, QB_EPOCH_HI, "EpochHi");
    CHECK_EQ(q.NhiBdf, 0x0300, "NhiBdf");
    CHECK_EQ(q.ContentGen, 3, "ContentGen");
    CHECK(strcmp(q.Id, QB_ID) == 0, "Id");
    CHECK(q.Rec == qa + 64, "Rec");
    CHECK_EQ(q.RecBytes, 128 + 256, "RecBytes");
    CHECK_EQ(q.ExtOffset, extOff, "ExtOffset");
    CHECK_EQ(q.ExtCount, 2, "ExtCount");
    CHECK_EQ(q.ExtBytes, 32, "ExtBytes");

    CHECK(tr_query_ext(&q, 1UL, &x) == 1, "extension 1");
    CHECK_EQ(x.Rate, 20, "ext Rate");
    CHECK_EQ(x.Width, 2, "ext Width");
    CHECK_EQ(x.LinkState, 8, "ext LinkState");
    CHECK_EQ(x.GenRaw, 3, "ext GenRaw");
    CHECK_EQ(x.Ids, 0x00011235UL, "ext Ids");
    CHECK_EQ(x.Flags, CM_Q_XF_RATE_READ, "ext Flags");
    CHECK(tr_query_ext(&q, 0UL, &x) == 1 && x.LinkState == 7, "extension 0");
    CHECK(tr_query_ext(&q, 2UL, &x) == 0, "no extension 2");
    CHECK(x.Rate == 0 && x.Width == 0, "a missing extension is zeroed");

    /* The sizing answer and a short read. */
    CHECK(tr_query_parse(qa, 64, &q) == -1, "the header alone asks for more");
    CHECK_EQ(q.Bytes, n, "the size asked for");
    CHECK(tr_query_parse(qa, n - 1, &q) == -1, "one byte short asks for more");
    CHECK(tr_query_parse(qa, 63, &q) == 0, "less than a header refused");

    put32(qa, CM_Q_O_BYTES, CM_Q_MAX_BYTES + 1UL);
    CHECK(tr_query_parse(qa, 64, &q) == 0, "Bytes past CM_Q_MAX_BYTES refused");
    put32(qa, CM_Q_O_BYTES, 100UL);
    CHECK(tr_query_parse(qa, n, &q) == 0, "Bytes below header and record refused");
    put32(qa, CM_Q_O_BYTES, (TR_U32)n);

    put32(qa, CM_Q_O_EPOCH_LO, 0UL);
    put32(qa, CM_Q_O_EPOCH_HI, 0UL);
    CHECK(tr_query_parse(qa, n, &q) == 0, "epoch 0 refused");
    CHECK(tr_query_parse(qa, 64, &q) == 0, "epoch 0 refused when sizing");
    put32(qa, CM_Q_O_EPOCH_LO, 0x11111111UL);
    CHECK(tr_query_parse(qa, n, &q) == 1, "epoch low 0 alone is fine");
    put32(qa, CM_Q_O_EPOCH_HI, QB_EPOCH_HI);

    put32(qa, CM_Q_O_CONTENT_GEN, 0UL);
    CHECK(tr_query_parse(qa, n, &q) == 0, "content generation 0 refused");
    put32(qa, CM_Q_O_CONTENT_GEN, 3UL);

    put32(qa, CM_Q_O_MAGIC, 0x514D4359UL);
    CHECK(tr_query_parse(qa, n, &q) == 0, "bad magic refused");
    put32(qa, CM_Q_O_MAGIC, CM_Q_MAGIC);
    put32(qa, CM_Q_O_VERSION, 2UL);
    CHECK(tr_query_parse(qa, n, &q) == 0, "bad version refused");
    put32(qa, CM_Q_O_VERSION, CM_Q_VERSION);

    put32(qa, CM_Q_O_EXT_SHAPE, 1UL | (32UL << 16));
    CHECK(tr_query_parse(qa, n, &q) == 0, "ext count against router count");
    put32(qa, CM_Q_O_EXT_SHAPE, 2UL | (16UL << 16));
    CHECK(tr_query_parse(qa, n, &q) == 0, "ext entries below 32 bytes");
    put32(qa, CM_Q_O_EXT_SHAPE, 2UL | (32UL << 16));

    put32(qa, CM_Q_O_EXT_OFFSET, (TR_U32)n - 32UL);
    CHECK(tr_query_parse(qa, n, &q) == 0, "ext region past Bytes");
    put32(qa, CM_Q_O_EXT_OFFSET, extOff - 4UL);
    CHECK(tr_query_parse(qa, n, &q) == 0, "ext region inside the record");
    put32(qa, CM_Q_O_EXT_OFFSET, extOff);

    put32(qa, CM_Q_O_ID_BYTES, (TR_U32)strlen(QB_ID) + 1UL);
    CHECK(tr_query_parse(qa, n, &q) == 0, "id past the end");
    put32(qa, CM_Q_O_ID_BYTES, 0UL);
    CHECK(tr_query_parse(qa, n, &q) == 0, "empty id");
    put32(qa, CM_Q_O_ID_BYTES, (TR_U32)strlen(QB_ID));
    put32(qa, CM_Q_O_ID_OFFSET, idOff - 1UL);
    CHECK(tr_query_parse(qa, n, &q) == 0, "id inside the extensions");
    put32(qa, CM_Q_O_ID_OFFSET, idOff);
    qa[idOff + 3] = 0x07;
    CHECK(tr_query_parse(qa, n, &q) == 0, "non-printable id");
    qa[idOff + 3] = 0x80;
    CHECK(tr_query_parse(qa, n, &q) == 0, "non-ASCII id");
    qa[idOff + 3] = '\\';

    put32(qa, CM_Q_O_REC_OFFSET, 32UL);
    CHECK(tr_query_parse(qa, n, &q) == 0, "record inside the header");
    put32(qa, CM_Q_O_REC_OFFSET, 64UL);
    put32(qa, 64 + CM_ST_O_ENTRY_BYTES, 64UL);
    CHECK(tr_query_parse(qa, n, &q) == 0, "a record inst_st_check refuses");
    put32(qa, 64 + CM_ST_O_ENTRY_BYTES, CM_ST_ENTRY_BYTES);
    put32(qa, 64 + CM_ST_O_BYTES, 0x10000UL);
    CHECK(tr_query_parse(qa, n, &q) == 0, "a record bigger than the answer");
    put32(qa, 64 + CM_ST_O_BYTES, 128UL + 256UL);

    CHECK(tr_query_parse(qa, n, &q) == 1, "restored answer parses");

    /* Current. */
    CHECK(tr_query_current(&q), "current");
    q.AgeMs = CM_Q_CURRENT_MS;
    CHECK(!tr_query_current(&q), "too old");
    q.AgeMs = CM_Q_UNKNOWN;
    CHECK(!tr_query_current(&q), "never published");
    q.AgeMs = 0;
    q.Flags = CM_Q_F_INERT;
    CHECK(!tr_query_current(&q), "inert is not current");
    q.Flags = 0;
    q.Live = 2;
    CHECK(!tr_query_current(&q), "not answering is not current");
    q.Live = 1;

    /* The eject's first gate. */
    why = "x";
    CHECK(tr_query_eject_ready(&q, &why) == 1, "eject ready");
    CHECK(why == NULL, "eject ready: no reason");
    CHECK(tr_query_eject_ready(&q, NULL) == 1, "eject ready without a reason");
    q.Flags = CM_Q_F_STOPPING | CM_Q_F_INERT;
    CHECK(tr_query_eject_ready(&q, &why) == 0 && has(why, "stopping"),
          "eject: stopping first");
    q.Flags = CM_Q_F_INERT;
    CHECK(tr_query_eject_ready(&q, &why) == 0 && has(why, "idle"),
          "eject: inert");
    q.Flags = CM_Q_F_TRUNCATED;
    put32(qa, 64 + CM_ST_O_STAGE, CM_ST_STAGE_FAILED);
    CHECK(tr_query_eject_ready(&q, &why) == 0 && has(why, "not active"),
          "eject: stage not active");
    put32(qa, 64 + CM_ST_O_STAGE, CM_ST_STAGE_ACTIVE);
    q.Live = 3;
    CHECK(tr_query_eject_ready(&q, &why) == 0 && has(why, "not answering"),
          "eject: live not active");
    q.Live = 1;
    q.Pass = 0;
    CHECK(tr_query_eject_ready(&q, &why) == 0 && has(why, "not published"),
          "eject: pass 0");
    q.Pass = 5;
    CHECK(tr_query_eject_ready(&q, &why) == 1, "eject: truncated is no bar");

    /* Passes and content. */
    n2 = qb_build(qb, 2UL, 0x11111111UL, 6UL);
    CHECK(tr_query_parse(qb, n2, &q2) == 1, "second answer parses");
    CHECK(!tr_query_new_pass(&q, &q2), "one pass on is not enough");
    q2.Pass = 7;
    CHECK(tr_query_new_pass(&q, &q2), "two passes on is new");
    q2.Pass = 9;
    CHECK(tr_query_new_pass(&q, &q2), "four passes on is new");
    q2.Pass = 5;
    CHECK(!tr_query_new_pass(&q, &q2), "same pass is not new");
    q2.Pass = 0;
    CHECK(!tr_query_new_pass(&q, &q2), "pass 0 is never new");
    q.Pass = 0xFFFFFFFFUL;
    q2.Pass = 1;
    CHECK(!tr_query_new_pass(&q, &q2), "a wrap to 1 is one publication");
    q2.Pass = 2;
    CHECK(tr_query_new_pass(&q, &q2), "a wrap to 2 is two publications");
    q.Pass = 0xFFFFFFFEUL;
    q2.Pass = 1;
    CHECK(tr_query_new_pass(&q, &q2), "0xFFFFFFFE to 1 is two publications");
    q.Pass = 0;
    q2.Pass = 2;
    CHECK(tr_query_new_pass(&q, &q2), "from before the first, two on");
    q.Pass = 5;
    q2.Pass = 7;

    CHECK(tr_query_same_content(&q, &q2), "same epoch and content");
    put32(qb, 64 + CM_ST_O_GENERATION, 8UL);
    CHECK(tr_query_same_content(&q, &q2),
          "the record's generation alone is not content");
    put32(qb, 64 + CM_ST_O_GENERATION, 7UL);
    q2.ContentGen = 4;
    CHECK(!tr_query_same_content(&q, &q2), "another content generation");
    q2.ContentGen = 3;
    q2.EpochLo = 0x22222222UL;
    CHECK(!tr_query_new_pass(&q, &q2), "another epoch is not a new pass");
    CHECK(!tr_query_same_content(&q, &q2), "another epoch is not the same content");
    q2.EpochLo = 0x11111111UL;
    q2.EpochHi = 1UL;
    CHECK(!tr_query_new_pass(&q, &q2), "another epoch high");
    CHECK(!tr_query_same_content(&q, &q2), "another epoch high, content");

    /* No routers. */
    n = qb_build(qa, 0UL, 0x11111111UL, 1UL);
    CHECK(tr_query_parse(qa, n, &q) == 1, "an answer with no routers");
    CHECK_EQ(q.ExtCount, 0, "no extensions");
    CHECK(tr_query_ext(&q, 0UL, &x) == 0, "no extension 0");
}

/* ---- texts ---------------------------------------------------------------- */

static void test_texts(void)
{
    static const char *const live[9] = {
        "not monitored", "answering", "not answering (suspect)", "gone",
        "configuration lost", "recovering", "recovery failed", "contained",
        "access revoked"
    };
    char out[96];
    unsigned char e[128];
    TR_EXT x;
    TR_U32 i;

    for (i = 0; i < 9; i++) {
        CHECK(strcmp(tr_live_text(i), live[i]) == 0, "live text 0..8");
    }
    CHECK(strcmp(tr_live_text(9UL), "state not known") == 0, "live text 9");

    CHECK(strcmp(tr_tun_text(CM_ST_TUN_NONE), "none") == 0, "tun none");
    CHECK(strcmp(tr_tun_text(CM_ST_TUN_UP), "up") == 0, "tun up");
    CHECK(strcmp(tr_tun_text(0x110UL | CM_ST_TUN_PRESERVED),
                 "up (found at start)") == 0, "tun: the PCIe nibble alone");
    CHECK(strcmp(tr_tun_text(CM_ST_TUN_FAILED), "failed") == 0, "tun failed");
    CHECK(strcmp(tr_tun_text(CM_ST_TUN_EJECTED), "closed after an eject") == 0,
          "tun ejected");
    CHECK(strcmp(tr_tun_text(CM_ST_TUN_UNKNOWN), "not known") == 0, "tun unknown");

    CHECK(strcmp(tr_appr_text(CM_ST_APPR_APPROVED), "approved") == 0, "appr 1");
    CHECK(strcmp(tr_appr_text(CM_ST_APPR_BOOT),
                 "approved before Windows started") == 0, "appr 2");
    CHECK(strcmp(tr_appr_text(CM_ST_APPR_REJECTED),
                 "rejected by the controller") == 0, "appr 3");
    CHECK(strcmp(tr_appr_text(CM_ST_APPR_FAILED),
                 "approval refused by the controller") == 0, "appr 4");
    CHECK(strcmp(tr_appr_text(CM_ST_APPR_UNANSWERED), "approval not answered") == 0,
          "appr 5");
    CHECK(strcmp(tr_appr_text(CM_ST_APPR_PENDING), "not approved") == 0, "appr 6");
    CHECK(strcmp(tr_appr_text(CM_ST_APPR_HELD), "connection held or failed") == 0,
          "appr 7");
    CHECK(strcmp(tr_appr_text(CM_ST_APPR_UNKNOWN), "approval not known") == 0,
          "appr 0");

    CHECK(strcmp(tr_cm_text(CM_ST_CM_ICM), "ICM") == 0, "cm ICM");
    CHECK(strcmp(tr_cm_text(CM_ST_CM_USB4), "USB4 software CM") == 0, "cm USB4");
    CHECK(strcmp(tr_cm_text(CM_ST_CM_NONE), "no connection manager") == 0, "cm none");

    CHECK(strcmp(tr_stage_text(CM_ST_STAGE_STOPPED), "stopped") == 0, "stage 0");
    CHECK(strcmp(tr_stage_text(CM_ST_STAGE_INERT), "idle (inert)") == 0, "stage 1");
    CHECK(strcmp(tr_stage_text(CM_ST_STAGE_ACTIVE), "active") == 0, "stage 2");
    CHECK(strcmp(tr_stage_text(CM_ST_STAGE_FAILED), "failed") == 0, "stage 3");
    CHECK(strcmp(tr_stage_text(4UL), "stage not known") == 0, "stage 4");

    tr_gen_text(CM_ST_GEN_TBT3, out, sizeof(out));
    CHECK(strcmp(out, "Thunderbolt 3") == 0, "gen TBT3");
    tr_gen_text(CM_ST_GEN_USB4 | 0x1000UL, out, sizeof(out));
    CHECK(strcmp(out, "USB4 v1.0") == 0, "gen USB4 v1.0");
    tr_gen_text(CM_ST_GEN_USB4 | 0x2000UL, out, sizeof(out));
    CHECK(strcmp(out, "USB4 v2.0") == 0, "gen USB4 v2.0");
    tr_gen_text(CM_ST_GEN_USB4, out, sizeof(out));
    CHECK(strcmp(out, "USB4") == 0, "gen USB4 without version");
    tr_gen_text(CM_ST_GEN_UNKNOWN, out, sizeof(out));
    CHECK(strcmp(out, "generation not known") == 0, "gen unknown");
    tr_gen_text(7UL, out, sizeof(out));
    CHECK(strcmp(out, "generation not known") == 0, "gen 7");

    memset(&x, 0, sizeof(x));
    x.Rate = 20;
    x.Width = 2;
    tr_link_text(CM_ST_KIND_USB4_PORT, CM_ST_LANES_SINGLE, &x, out, sizeof(out));
    CHECK(has(out, "Gen 3") && has(out, "x2") && has(out, "40 Gbit/s"),
          "link: USB4 Gen 3 x2");
    CHECK(strcmp(out, "USB4 Gen 3 x2 (40 Gbit/s)") == 0, "link: exact");
    x.Rate = 40;
    x.Width = 0;
    tr_link_text(CM_ST_KIND_USB4_LINK, CM_ST_LANES_BONDED, &x, out, sizeof(out));
    CHECK(strcmp(out, "USB4 Gen 4 x2 (80 Gbit/s)") == 0,
          "link: width from the record's lanes");
    x.Rate = 10;
    tr_link_text(CM_ST_KIND_USB4_PORT, CM_ST_LANES_UNKNOWN, &x, out, sizeof(out));
    CHECK(strcmp(out, "USB4 Gen 2 (10 Gbit/s per lane), lanes not known") == 0,
          "link: rate without width");
    tr_link_text(CM_ST_KIND_ICM, CM_ST_LANES_BONDED, NULL, out, sizeof(out));
    CHECK(has(out, "not reported by the firmware"), "link: ICM without ext");
    CHECK(strcmp(out, "two lanes bonded; speed not reported by the firmware") == 0,
          "link: ICM exact");
    memset(&x, 0, sizeof(x));
    tr_link_text(CM_ST_KIND_ICM, CM_ST_LANES_UNKNOWN, &x, out, sizeof(out));
    CHECK(strcmp(out, "lanes not known; speed not reported by the firmware") == 0,
          "link: ICM, an empty extension");
    tr_link_text(CM_ST_KIND_USB4_PORT, CM_ST_LANES_SINGLE, NULL, out, sizeof(out));
    CHECK(strcmp(out, "one lane; speed not known") == 0, "link: lanes only");
    tr_link_text(CM_ST_KIND_USB4_PORT, CM_ST_LANES_BONDED, NULL, out, sizeof(out));
    CHECK(strcmp(out, "two lanes; speed not known") == 0, "link: two lanes only");

    memset(e, 0, sizeof(e));
    memcpy(e + CM_ST_E_VENDOR, "Acme", 4);
    memcpy(e + CM_ST_E_MODEL, "Dock", 4);
    tr_router_name(e, out, sizeof(out));
    CHECK(strcmp(out, "Acme Dock") == 0, "name: vendor and model");
    memset(e + CM_ST_E_MODEL, 0, 32);
    tr_router_name(e, out, sizeof(out));
    CHECK(strcmp(out, "Acme") == 0, "name: vendor alone");
    memset(e + CM_ST_E_VENDOR, 0, 32);
    memcpy(e + CM_ST_E_MODEL, "D\001ck", 4);
    tr_router_name(e, out, sizeof(out));
    CHECK(strcmp(out, "D?ck") == 0, "name: model alone, unprintable as '?'");
    memset(e, 0, sizeof(e));
    tr_router_name(e, out, sizeof(out));
    CHECK(strcmp(out, "(no name given)") == 0, "name: none given");
    memset(e + CM_ST_E_VENDOR, 'V', 32);
    tr_router_name(e, out, sizeof(out));
    CHECK_EQ(strlen(out), 31, "name: an unterminated vendor stops at 31");
}

/* ---- the tray's events ---------------------------------------------------- */

/* One NHI's answer as a valid snapshot. */
static void snap_of(TR_SNAP *s, const unsigned char *b, size_t n, TR_U32 live)
{
    TR_QUERY q;

    tr_snap_init(s);
    check_impl(tr_query_parse(b, n, &q) == 1, "snapshot's answer parses",
               __FILE__, __LINE__);
    q.Live = live;
    check_impl(tr_snap_add(s, 1UL, &q) == 1, "snapshot added", __FILE__,
               __LINE__);
    s->Valid = 1;
}

static TR_SNAP so;
static TR_SNAP sn;

static void test_events(void)
{
    TR_EVENT ev[8];
    TR_QUERY q;
    char out[160];
    size_t n;
    int k;
    int i;
    TR_SROUTER a;
    TR_SROUTER b;

    /* A snapshot as added. */
    n = qb_build(qa, 2UL, 0x11111111UL, 5UL);
    qb_entry(qa, 0UL, CM_ST_KIND_USB4_PORT, 0x0101UL, 1UL, CM_ST_APPR_APPROVED,
             CM_ST_TUN_UP, CM_ST_GEN_USB4 | 0x1000UL, "Acme", "Dock");
    qb_entry(qa, 1UL, CM_ST_KIND_ICM, 0x0102UL, 0UL, CM_ST_APPR_PENDING,
             CM_ST_TUN_NONE, CM_ST_GEN_TBT3, "", "");
    snap_of(&so, qa, n, 1UL);
    CHECK_EQ(so.NhiCount, 1, "snap: one NHI");
    CHECK_EQ(so.NhiKey[0], 1, "snap: its key");
    CHECK_EQ(so.Live[0], 1, "snap: its liveness");
    CHECK_EQ(so.EpochLo[0], 0x11111111UL, "snap: its epoch");
    CHECK_EQ(so.Count, 2, "snap: two routers");
    CHECK_EQ(so.R[0].Kind, CM_ST_KIND_USB4_PORT, "snap: kind");
    CHECK_EQ(so.R[0].RouteLo, 1, "snap: route");
    CHECK_EQ(so.R[0].Place, 0x0101, "snap: place");
    CHECK_EQ(so.R[0].Tunnels, CM_ST_TUN_UP, "snap: tunnels");
    CHECK_EQ(so.R[0].Bridge, 0x0500, "snap: bridge");
    CHECK_EQ(so.R[0].Gen, CM_ST_GEN_USB4 | 0x1000UL, "snap: gen");
    CHECK(strcmp(so.R[0].Name, "Acme Dock") == 0, "snap: name");
    CHECK_EQ(so.R[1].Ids, 0x12348086UL, "snap: ICM ids");
    CHECK_EQ(so.R[1].ShortId, 0xABCD, "snap: ICM short id");
    CHECK_EQ(so.R[1].Approval, CM_ST_APPR_PENDING, "snap: approval");
    CHECK(strcmp(so.R[1].Name, "(no name given)") == 0, "snap: no name");
    CHECK(tr_query_parse(qa, n, &q) == 1, "snap: answer again");
    for (k = 1; k < TR_SNAP_NHI; k++) {
        CHECK(tr_snap_add(&so, (TR_U32)k + 1UL, &q) == 1, "snap: more NHIs");
    }
    CHECK(tr_snap_add(&so, 9UL, &q) == 0, "snap: a fifth NHI refused");
    CHECK(so.Truncated == 1, "snap: truncated");
    CHECK_EQ(so.Count, 8, "snap: the routers of four NHIs");

    /* Nothing without a valid old snapshot. */
    n = qb_build(qa, 1UL, 0x11111111UL, 5UL);
    qb_entry(qa, 0UL, CM_ST_KIND_USB4_PORT, 0x0101UL, 1UL, CM_ST_APPR_APPROVED,
             CM_ST_TUN_UP, CM_ST_GEN_USB4 | 0x1000UL, "Acme", "Dock");
    snap_of(&sn, qa, n, 1UL);
    tr_snap_init(&so);
    CHECK_EQ(tr_diff(&so, &sn, ev, 8), 0, "diff: old invalid");
    CHECK_EQ(tr_diff(&sn, &so, ev, 8), 0, "diff: new invalid");

    /* A USB4 router appears. */
    n = qb_build(qb, 0UL, 0x11111111UL, 4UL);
    snap_of(&so, qb, n, 1UL);
    k = tr_diff(&so, &sn, ev, 8);
    CHECK_EQ(k, 1, "diff: one router appeared");
    CHECK_EQ(ev[0].Kind, TR_EV_CONNECTED, "diff: CONNECTED");
    CHECK(strcmp(ev[0].Name, "Acme Dock") == 0, "diff: with its name");
    tr_event_text(&ev[0], out, sizeof(out));
    CHECK(strcmp(out, "Acme Dock connected (USB4 v1.0).") == 0,
          "event text: connected");
    CHECK(strcmp(tr_event_title(ev[0].Kind), "Thunderbolt/USB4") == 0,
          "event title: connected");
    CHECK_EQ(tr_diff(&so, &sn, ev, 0), 0, "diff: cap 0");
    CHECK_EQ(tr_diff(&sn, &sn, ev, 8), 0, "diff: no change");

    /* Leaves: always "was disconnected", whatever its tunnels showed. */
    k = tr_diff(&sn, &so, ev, 8);
    CHECK_EQ(k, 1, "diff: one router left");
    CHECK_EQ(ev[0].Kind, TR_EV_LEFT, "diff: LEFT");
    CHECK(strcmp(ev[0].Name, "Acme Dock") == 0, "diff: LEFT with its name");
    tr_event_text(&ev[0], out, sizeof(out));
    CHECK(strcmp(out, "Acme Dock was disconnected.") == 0, "event text: left");
    CHECK(has(out, "was disconnected"), "event text: disconnected");
    CHECK(strcmp(tr_event_title(TR_EV_LEFT), "Thunderbolt/USB4") == 0,
          "event title: left");
    sn.R[0].Tunnels = 0x110UL | CM_ST_TUN_PRESERVED;
    k = tr_diff(&sn, &so, ev, 8);
    CHECK(k == 1 && ev[0].Kind == TR_EV_LEFT, "diff: preserved tunnel LEFT");
    sn.R[0].Tunnels = CM_ST_TUN_FAILED;
    k = tr_diff(&sn, &so, ev, 8);
    CHECK(k == 1 && ev[0].Kind == TR_EV_LEFT, "diff: failed tunnel LEFT");

    /* An ICM entry leaving, and appearing. */
    n = qb_build(qa, 1UL, 0x11111111UL, 5UL);
    qb_entry(qa, 0UL, CM_ST_KIND_ICM, 0x0102UL, 0UL, CM_ST_APPR_APPROVED,
             CM_ST_TUN_UP, CM_ST_GEN_TBT3, "Apple", "Display");
    snap_of(&sn, qa, n, 1UL);
    k = tr_diff(&sn, &so, ev, 8);
    CHECK_EQ(k, 1, "diff: ICM left");
    CHECK_EQ(ev[0].Kind, TR_EV_LEFT, "diff: ICM LEFT");
    CHECK(strcmp(ev[0].Name, "Apple Display") == 0, "diff: ICM name");
    k = tr_diff(&so, &sn, ev, 8);
    CHECK(k == 1 && ev[0].Kind == TR_EV_CONNECTED, "diff: ICM connected");
    tr_event_text(&ev[0], out, sizeof(out));
    CHECK(strcmp(out, "Apple Display connected (Thunderbolt 3).") == 0,
          "event text: ICM connected");

    /* ICM approval: pending appears; then changes to failed. */
    n = qb_build(qa, 1UL, 0x11111111UL, 5UL);
    qb_entry(qa, 0UL, CM_ST_KIND_ICM, 0x0102UL, 0UL, CM_ST_APPR_PENDING,
             CM_ST_TUN_NONE, CM_ST_GEN_UNKNOWN, "Acme", "Dock");
    snap_of(&sn, qa, n, 1UL);
    k = tr_diff(&so, &sn, ev, 8);
    CHECK(k == 1 && ev[0].Kind == TR_EV_WAITING, "diff: ICM WAITING appears");
    tr_event_text(&ev[0], out, sizeof(out));
    CHECK(strcmp(out, "Acme Dock is waiting to be approved.") == 0,
          "event text: waiting");
    put32(qa, qb_eoff(0UL) + CM_ST_E_APPROVAL, CM_ST_APPR_FAILED);
    snap_of(&so, qa, n, 1UL);
    k = tr_diff(&sn, &so, ev, 8);
    CHECK(k == 1 && ev[0].Kind == TR_EV_FAILED, "diff: ICM pending to FAILED");
    tr_event_text(&ev[0], out, sizeof(out));
    CHECK(strcmp(out, "Acme Dock could not be approved.") == 0,
          "event text: failed");
    CHECK(strcmp(tr_event_title(TR_EV_FAILED), "Thunderbolt/USB4 warning") == 0,
          "event title: failed");
    CHECK_EQ(tr_diff(&so, &so, ev, 8), 0, "diff: failed stays failed");
    k = tr_diff(&so, &sn, ev, 8);
    CHECK(k == 1 && ev[0].Kind == TR_EV_WAITING, "diff: ICM failed back to pending");
    put32(qa, qb_eoff(0UL) + CM_ST_E_APPROVAL, CM_ST_APPR_APPROVED);
    snap_of(&so, qa, n, 1UL);
    CHECK_EQ(tr_diff(&sn, &so, ev, 8), 0, "diff: pending to approved is quiet");
    put32(qa, qb_eoff(0UL) + CM_ST_E_APPROVAL, CM_ST_APPR_UNANSWERED);
    snap_of(&so, qa, n, 1UL);
    CHECK_EQ(tr_diff(&sn, &so, ev, 8), 0, "diff: pending to unanswered is quiet");
    put32(qa, qb_eoff(0UL) + CM_ST_E_APPROVAL, CM_ST_APPR_REJECTED);
    snap_of(&so, qa, n, 1UL);
    k = tr_diff(&sn, &so, ev, 8);
    CHECK(k == 1 && ev[0].Kind == TR_EV_FAILED, "diff: ICM pending to rejected");
    snap_of(&sn, qb, qb_build(qb, 0UL, 0x11111111UL, 4UL), 1UL);
    k = tr_diff(&sn, &so, ev, 8);
    CHECK(k == 1 && ev[0].Kind == TR_EV_FAILED, "diff: ICM appears rejected");
    tr_event_text(&ev[0], out, sizeof(out));
    CHECK(strcmp(out, "Acme Dock could not be approved.") == 0,
          "event text: appears failed");
    put32(qa, qb_eoff(0UL) + CM_ST_E_APPROVAL, CM_ST_APPR_UNANSWERED);
    snap_of(&so, qa, n, 1UL);
    k = tr_diff(&sn, &so, ev, 8);
    CHECK(k == 1 && ev[0].Kind == TR_EV_WAITING, "diff: ICM appears unanswered");
    put32(qa, qb_eoff(0UL) + CM_ST_E_APPROVAL, CM_ST_APPR_HELD);
    snap_of(&so, qa, n, 1UL);
    k = tr_diff(&sn, &so, ev, 8);
    CHECK(k == 1 && ev[0].Kind == TR_EV_CONNECTED,
          "diff: HELD is not waiting");

    /* USB4 entries have no approval events: CONNECTED and LEFT only. */
    n = qb_build(qa, 1UL, 0x11111111UL, 5UL);
    qb_entry(qa, 0UL, CM_ST_KIND_USB4_PORT, 0x0101UL, 1UL, CM_ST_APPR_PENDING,
             CM_ST_TUN_NONE, CM_ST_GEN_UNKNOWN, "Acme", "Dock");
    snap_of(&so, qa, n, 1UL);
    k = tr_diff(&sn, &so, ev, 8);
    CHECK(k == 1 && ev[0].Kind == TR_EV_CONNECTED,
          "diff: a pending USB4 router appears CONNECTED");
    put32(qa, qb_eoff(0UL) + CM_ST_E_APPROVAL, CM_ST_APPR_FAILED);
    snap_of(&sn, qa, n, 1UL);
    CHECK_EQ(tr_diff(&so, &sn, ev, 8), 0, "diff: USB4 pending to failed is quiet");
    put32(qa, qb_eoff(0UL) + CM_ST_E_APPROVAL, CM_ST_APPR_HELD);
    snap_of(&sn, qa, n, 1UL);
    CHECK_EQ(tr_diff(&so, &sn, ev, 8), 0, "diff: USB4 pending to held is quiet");
    snap_of(&sn, qb, qb_build(qb, 0UL, 0x11111111UL, 4UL), 1UL);
    put32(qa, qb_eoff(0UL) + CM_ST_E_APPROVAL, CM_ST_APPR_REJECTED);
    snap_of(&so, qa, n, 1UL);
    k = tr_diff(&sn, &so, ev, 8);
    CHECK(k == 1 && ev[0].Kind == TR_EV_CONNECTED,
          "diff: a rejected USB4 router appears CONNECTED");
    k = tr_diff(&so, &sn, ev, 8);
    CHECK(k == 1 && ev[0].Kind == TR_EV_LEFT, "diff: a USB4 router leaves LEFT");

    /* The liveness leaves ACTIVE. */
    n = qb_build(qa, 1UL, 0x11111111UL, 5UL);
    qb_entry(qa, 0UL, CM_ST_KIND_USB4_PORT, 0x0101UL, 1UL, CM_ST_APPR_APPROVED,
             CM_ST_TUN_UP, CM_ST_GEN_USB4, "Acme", "Dock");
    snap_of(&so, qa, n, 1UL);
    snap_of(&sn, qa, n, 2UL);
    k = tr_diff(&so, &sn, ev, 8);
    CHECK_EQ(k, 1, "diff: one stop event");
    CHECK_EQ(ev[0].Kind, TR_EV_STOPPED, "diff: STOPPED");
    tr_event_text(&ev[0], out, sizeof(out));
    CHECK(strcmp(out, "The Thunderbolt/USB4 controller stopped answering.") == 0,
          "event text: stopped");
    CHECK(strcmp(tr_event_title(TR_EV_STOPPED), "Thunderbolt/USB4 warning") == 0,
          "event title: stopped");
    CHECK_EQ(tr_diff(&sn, &so, ev, 8), 0, "diff: back to ACTIVE is quiet");
    snap_of(&so, qa, n, 0UL);
    CHECK_EQ(tr_diff(&so, &sn, ev, 8), 0, "diff: OFF to SUSPECT is quiet");

    /* A new epoch is a new set of devices. */
    n = qb_build(qa, 1UL, 0x11111111UL, 5UL);
    qb_entry(qa, 0UL, CM_ST_KIND_USB4_PORT, 0x0101UL, 1UL, CM_ST_APPR_APPROVED,
             CM_ST_TUN_UP, CM_ST_GEN_USB4, "Acme", "Dock");
    snap_of(&so, qa, n, 1UL);
    snap_of(&sn, qb, qb_build(qb, 0UL, 0x22222222UL, 1UL), 1UL);
    CHECK_EQ(tr_diff(&so, &sn, ev, 8), 0, "diff: epoch change, router gone");
    CHECK_EQ(tr_diff(&sn, &so, ev, 8), 0, "diff: epoch change, router new");
    tr_snap_init(&sn);
    sn.Valid = 1;
    CHECK_EQ(tr_diff(&so, &sn, ev, 8), 0, "diff: the NHI gone");

    /* ICM entries sharing a key: no identity, one generic event. */
    n = qb_build(qa, 2UL, 0x11111111UL, 5UL);
    qb_entry(qa, 0UL, CM_ST_KIND_ICM, 0x0102UL, 0UL, CM_ST_APPR_APPROVED,
             CM_ST_TUN_UP, CM_ST_GEN_TBT3, "Apple", "Display");
    qb_entry(qa, 1UL, CM_ST_KIND_ICM, 0x0102UL, 0UL, CM_ST_APPR_APPROVED,
             CM_ST_TUN_UP, CM_ST_GEN_TBT3, "Apple", "Display");
    snap_of(&so, qa, n, 1UL);
    n = qb_build(qb, 1UL, 0x11111111UL, 6UL);
    qb_entry(qb, 0UL, CM_ST_KIND_ICM, 0x0102UL, 0UL, CM_ST_APPR_APPROVED,
             CM_ST_TUN_UP, CM_ST_GEN_TBT3, "Apple", "Display");
    snap_of(&sn, qb, n, 1UL);
    k = tr_diff(&so, &sn, ev, 8);
    CHECK_EQ(k, 1, "diff: duplicates two to one, one event");
    CHECK_EQ(ev[0].Kind, TR_EV_LEFT, "diff: duplicates LEFT");
    CHECK(strcmp(ev[0].Name, "A Thunderbolt device") == 0,
          "diff: duplicates have a generic name");
    tr_event_text(&ev[0], out, sizeof(out));
    CHECK(strcmp(out, "A Thunderbolt device was disconnected.") == 0,
          "event text: generic left");
    k = tr_diff(&sn, &so, ev, 8);
    CHECK_EQ(k, 1, "diff: duplicates one to two, one event");
    CHECK_EQ(ev[0].Kind, TR_EV_CONNECTED, "diff: duplicates CONNECTED");
    CHECK(strcmp(ev[0].Name, "A Thunderbolt device") == 0,
          "diff: duplicates connected generic");
    CHECK_EQ(tr_diff(&so, &so, ev, 8), 0, "diff: duplicates unchanged");
    n = qb_build(qb, 3UL, 0x11111111UL, 6UL);
    for (i = 0; i < 3; i++) {
        qb_entry(qb, (TR_U32)i, CM_ST_KIND_ICM, 0x0102UL, 0UL,
                 CM_ST_APPR_PENDING, CM_ST_TUN_NONE, CM_ST_GEN_TBT3, "Apple",
                 "Display");
    }
    snap_of(&sn, qb, n, 1UL);
    k = tr_diff(&so, &sn, ev, 8);
    CHECK(k == 1 && ev[0].Kind == TR_EV_CONNECTED,
          "diff: duplicates two to three, one CONNECTED, never WAITING");
    k = tr_diff(&sn, &so, ev, 8);
    CHECK(k == 1 && ev[0].Kind == TR_EV_LEFT, "diff: three to two, one LEFT");

    /* tr_same_router. */
    memset(&a, 0, sizeof(a));
    a.Nhi = 1;
    a.EpochLo = 5;
    a.EpochHi = 6;
    a.Kind = CM_ST_KIND_USB4_LINK;
    a.RouteLo = 0x0301;
    a.RouteHi = 0;
    a.Place = 0x0201;
    b = a;
    b.Place = 0x0302;
    b.Approval = CM_ST_APPR_FAILED;
    tr_copy(b.Name, "other", sizeof(b.Name));
    CHECK(tr_same_router(&a, &b), "same: USB4 by route");
    b.RouteHi = 1;
    CHECK(!tr_same_router(&a, &b), "same: USB4 route high differs");
    b = a;
    b.RouteLo = 0x0401;
    CHECK(!tr_same_router(&a, &b), "same: USB4 route differs");
    b = a;
    b.Nhi = 2;
    CHECK(!tr_same_router(&a, &b), "same: another NHI");
    b = a;
    b.EpochHi = 7;
    CHECK(!tr_same_router(&a, &b), "same: another epoch");
    b = a;
    b.Kind = CM_ST_KIND_USB4_PORT;
    CHECK(!tr_same_router(&a, &b), "same: another kind");
    a.Kind = CM_ST_KIND_ICM;
    a.Ids = 0x12348086UL;
    a.ShortId = 0xABCD;
    b = a;
    b.RouteLo = 99;
    CHECK(tr_same_router(&a, &b), "same: ICM ignores the route");
    b.Place = 0x0202;
    CHECK(!tr_same_router(&a, &b), "same: ICM place differs");
    b = a;
    b.Ids = 0x12358086UL;
    CHECK(!tr_same_router(&a, &b), "same: ICM ids differ");
    b = a;
    b.ShortId = 0xABCE;
    CHECK(!tr_same_router(&a, &b), "same: ICM short id differs");

    CHECK(strcmp(tr_event_title(TR_EV_CONNECTED), "Thunderbolt/USB4") == 0,
          "title: connected");
    CHECK(strcmp(tr_event_title(TR_EV_WAITING), "Thunderbolt/USB4") == 0,
          "title: waiting");
    CHECK(strcmp(tr_event_title(TR_EV_LEFT), "Thunderbolt/USB4") == 0,
          "title: left");
    memset(&ev[0], 0, sizeof(ev[0]));
    ev[0].Kind = TR_EV_CONNECTED;
    tr_copy(ev[0].Name, "Box", sizeof(ev[0].Name));
    tr_event_text(&ev[0], out, sizeof(out));
    CHECK(strcmp(out, "Box connected.") == 0, "event text: generation unknown");
    ev[0].Kind = 0;
    tr_event_text(&ev[0], out, sizeof(out));
    CHECK(out[0] == '\0', "event text: unknown kind is empty");
}

/* ---- keys, the command line, the report ---------------------------------- */

static void test_keys_args_report(void)
{
    char out[64];
    char err[160];
    TR_OPTS o;
    TR_TEXT t;
    char *a0[1];
    char *a1[2];
    char *a2[3];
    char *a3[4];
    char *a4[5];

    tr_key(out, sizeof(out), "usb", "a", "b");
    CHECK(strcmp(out, "usb:a:b") == 0, "key");
    tr_key(out, sizeof(out), "tb", NULL, NULL);
    CHECK(strcmp(out, "tb::") == 0, "key without parts");
    tr_key(out, 5, "usb", "abc", "d");
    CHECK(strcmp(out, "usb:") == 0, "key bounded");
    tr_port_path(out, sizeof(out), NULL, 3UL);
    CHECK(strcmp(out, "3") == 0, "port path from the root, NULL");
    tr_port_path(out, sizeof(out), "", 12UL);
    CHECK(strcmp(out, "12") == 0, "port path from the root, empty");
    tr_port_path(out, sizeof(out), "3", 1UL);
    CHECK(strcmp(out, "3.1") == 0, "port path one deep");
    tr_port_path(out, sizeof(out), "3.1", 4UL);
    CHECK(strcmp(out, "3.1.4") == 0, "port path two deep");

    a0[0] = "xhcitr";
    a1[0] = "xhcitr";
    a2[0] = "xhcitr";
    a3[0] = "xhcitr";
    a4[0] = "xhcitr";

    memset(&o, 0x55, sizeof(o));
    CHECK_EQ(tr_parse_args(1, a0, &o, err, sizeof(err)), 0, "no switch");
    CHECK(o.Tray == 0 && o.Eject == 0 && o.Report == 0 && o.SelfTest == 0,
          "no switch: nothing set");
    CHECK(err[0] == '\0', "no switch: no error");

    a1[1] = "/tray";
    CHECK(tr_parse_args(2, a1, &o, err, sizeof(err)) == 0 && o.Tray == 1,
          "/tray");
    a1[1] = "-TRAY";
    CHECK(tr_parse_args(2, a1, &o, err, sizeof(err)) == 0 && o.Tray == 1,
          "-TRAY");
    a1[1] = "/SelfTest";
    CHECK(tr_parse_args(2, a1, &o, err, sizeof(err)) == 0 && o.SelfTest == 1,
          "/selftest");

    a2[1] = "/report";
    a2[2] = "C:\\out\\tree.txt";
    CHECK(tr_parse_args(3, a2, &o, err, sizeof(err)) == 0, "/report file");
    CHECK(o.Report == 1 && strcmp(o.ReportPath, "C:\\out\\tree.txt") == 0,
          "/report: its file");
    CHECK_EQ(tr_parse_args(2, a2, &o, err, sizeof(err)), 1,
             "/report without a file");
    CHECK(has(err, "/report needs"), "/report: why");
    a2[2] = "";
    CHECK_EQ(tr_parse_args(3, a2, &o, err, sizeof(err)), 1,
             "/report with an empty file");

    a2[1] = "/eject";
    a2[2] = "id=PCI\\VEN_8086&DEV_15EB\\4&1";
    CHECK(tr_parse_args(3, a2, &o, err, sizeof(err)) == 0, "/eject id=X");
    CHECK(o.Eject == 1 && strcmp(o.EjectId, "PCI\\VEN_8086&DEV_15EB\\4&1") == 0,
          "/eject: its id");
    a2[2] = "ID=x";
    CHECK(tr_parse_args(3, a2, &o, err, sizeof(err)) == 0 &&
          strcmp(o.EjectId, "x") == 0, "/eject ID= without case");
    CHECK_EQ(tr_parse_args(2, a2, &o, err, sizeof(err)), 1, "/eject without id");
    CHECK(has(err, "/eject needs id="), "/eject: why");
    a2[2] = "foo";
    CHECK_EQ(tr_parse_args(3, a2, &o, err, sizeof(err)), 1, "/eject foo");
    a2[2] = "id=";
    CHECK_EQ(tr_parse_args(3, a2, &o, err, sizeof(err)), 1, "/eject id= empty");
    a2[2] = "i";
    CHECK_EQ(tr_parse_args(3, a2, &o, err, sizeof(err)), 1, "/eject i");

    a3[1] = "/tray";
    a3[2] = "/report";
    a3[3] = "f";
    CHECK_EQ(tr_parse_args(4, a3, &o, err, sizeof(err)), 1, "/tray /report f");
    CHECK(has(err, "used alone"), "/tray /report: why");
    a2[1] = "/tray";
    a2[2] = "/selftest";
    CHECK_EQ(tr_parse_args(3, a2, &o, err, sizeof(err)), 1, "/tray /selftest");
    a2[1] = "/tray";
    a2[2] = "-tray";
    CHECK(tr_parse_args(3, a2, &o, err, sizeof(err)) == 0, "/tray twice");

    a4[1] = "/report";
    a4[2] = "a";
    a4[3] = "/eject";
    a4[4] = "id=b";
    CHECK_EQ(tr_parse_args(5, a4, &o, err, sizeof(err)), 1,
             "/report a /eject id=b");

    a1[1] = "/bogus";
    CHECK_EQ(tr_parse_args(2, a1, &o, err, sizeof(err)), 1, "unknown switch");
    CHECK(has(err, "/bogus"), "unknown switch named");
    a1[1] = "tray";
    CHECK_EQ(tr_parse_args(2, a1, &o, err, sizeof(err)), 1, "a switch needs / or -");
    CHECK(has(err, "tray"), "bare word named");

    tr_text_init(&t, big, sizeof(big));
    tr_report_node(&t, 1, "Port 3", "a\r\nb\nc");
    CHECK(strcmp(big, "  Port 3\r\n      | a\r\n      | b\r\n      | c\r\n") == 0,
          "report: depth, prefix, CR LF");
    tr_text_init(&t, big, sizeof(big));
    tr_report_node(&t, 0, "Root", NULL);
    CHECK(strcmp(big, "Root\r\n") == 0, "report: no details");
    tr_text_init(&t, big, sizeof(big));
    tr_report_node(&t, 2, "Hub", "x\r\n\r\ny\r\n");
    CHECK(strcmp(big, "    Hub\r\n        | x\r\n        | \r\n        | y\r\n") == 0,
          "report: an empty line, a trailing CR LF");
    tr_text_init(&t, big, sizeof(big));
    tr_report_node(&t, 0, "A", "");
    CHECK(strcmp(big, "A\r\n") == 0, "report: empty details");
}

static void test_sizes(void)
{
    CHECK_EQ(sizeof(TR_USB_CONN_EX), 35, "sizeof TR_USB_CONN_EX");
    CHECK_EQ(sizeof(TR_USB_CONN_V2), 16, "sizeof TR_USB_CONN_V2");
    CHECK_EQ(sizeof(TR_USB_DESC_REQ), 12, "sizeof TR_USB_DESC_REQ");
    CHECK_EQ(sizeof(TR_USB_CONN_EX), TR_CI_BYTES, "TR_CI_BYTES");
    CHECK_EQ(sizeof(TR_USB_CONN_V2), TR_V2_BYTES, "TR_V2_BYTES");
    CHECK_EQ(sizeof(TR_USB_DESC_REQ), TR_DR_BYTES, "TR_DR_BYTES");
    CHECK_EQ(offsetof(TR_USB_CONN_EX, NumberOfOpenPipes), 27, "OpenPipes at 27");
    CHECK_EQ(offsetof(TR_USB_CONN_EX, ConnectionStatus), 31, "Status at 31");
    CHECK_EQ(offsetof(TR_USB_DESC_REQ, wLength), 10, "wLength at 10");
#ifndef _WIN64
    CHECK_EQ(sizeof(TR_NIDA), 488, "sizeof TR_NIDA");
    CHECK_EQ(offsetof(TR_NIDA, szTip) + 64, TR_NIDA_V1_BYTES, "NIDA version 1");
    CHECK_EQ(offsetof(TR_NIDA, szInfo), 160, "szInfo at 160");
#endif
}

int main(void)
{
    test_printf();
    test_hex_names();
    test_conn();
    test_device_config();
    test_bos_hub_string();
    test_query();
    test_texts();
    test_events();
    test_keys_args_report();
    test_sizes();
    printf("test_tr: %d checks, %d failures\n", checks, failures);
    return failures;
}
