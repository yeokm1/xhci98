/*
 * test_tr.c - host vectors for XHCITR.EXE's pure half (xhcitr\tr_core.c;
 * design record 25 section 10; roadmap-usb4.md task 41-A.2): the bounded
 * printf, the USB IOCTL answers read as bytes, the descriptor decoders
 * over well-formed, truncated and malformed input, xhcicm.sys's query
 * answer and its refusals, the status record's texts, the tray's event
 * diff, the tree's keys, the command line, the report's layout, the
 * packed request sizes and the drivers' logging levels.
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
#include "../xhciinst/inst_core.h"
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

/* The answer qb_build lays out: version 2 (the removal block at 64, the
 * record after it) unless qbV1; the block's flags, count and member. */
static int qbV1;
static TR_U32 qbRmFlags;
static TR_U32 qbRmCount;
static TR_U32 qbRmMember = CM_Q_UNKNOWN;

static TR_U32 qb_rec(void)
{
    return qbV1 ? CM_Q_HDR_BYTES : CM_Q_HDR_BYTES + CM_Q_RM_BYTES;
}

static size_t qb_eoff(TR_U32 i)
{
    return (size_t)(qb_rec() + CM_ST_HDR_BYTES + i * CM_ST_ENTRY_BYTES);
}

/* A whole, valid answer with `routers` zeroed entries; its size. */
static size_t qb_build(unsigned char *b, TR_U32 routers, TR_U32 epochLo,
                       TR_U32 pass)
{
    TR_U32 recBytes = CM_ST_HDR_BYTES + routers * CM_ST_ENTRY_BYTES;
    TR_U32 extOff = qb_rec() + recBytes;
    TR_U32 idOff = extOff + routers * CM_Q_EXT_BYTES;
    TR_U32 idLen = (TR_U32)strlen(QB_ID);
    TR_U32 bytes = idOff + idLen;
    unsigned char *r = b + qb_rec();
    TR_U32 i;

    memset(b, 0, (size_t)bytes + 64);
    put32(b, CM_Q_O_MAGIC, CM_Q_MAGIC);
    put32(b, CM_Q_O_VERSION, qbV1 ? CM_Q_VERSION_1 : CM_Q_VERSION);
    if (!qbV1) {
        put32(b + CM_Q_HDR_BYTES, CM_Q_RM_FLAGS, qbRmFlags);
        put32(b + CM_Q_HDR_BYTES, CM_Q_RM_COUNT, qbRmCount);
        put32(b + CM_Q_HDR_BYTES, CM_Q_RM_MEMBER, qbRmMember);
    }
    put32(b, CM_Q_O_BYTES, bytes);
    put32(b, CM_Q_O_PASS, pass);
    put32(b, CM_Q_O_PASS_AGE_MS, 100UL);
    put32(b, CM_Q_O_LIVE, 1UL);
    put32(b, CM_Q_O_FLAGS, 0UL);
    put32(b, CM_Q_O_REC_OFFSET, qb_rec());
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
             CM_ST_TUN_UP, CM_ST_GEN_USB4 | 0x2000UL, "Acme", "Dock");
    qb_entry(qa, 1UL, CM_ST_KIND_ICM, 0x0102UL, 0UL, CM_ST_APPR_APPROVED,
             CM_ST_TUN_UP, CM_ST_GEN_TBT3, "Apple", "Display");
    CHECK_EQ(n, 96 + 128 + 256 + 64 + strlen(QB_ID), "the built answer's size");
    extOff = 96UL + 128UL + 256UL;
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
    CHECK(q.Rec == qa + 96, "Rec");
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
    put32(qa, CM_Q_O_VERSION, 3UL);
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
    CHECK(tr_query_parse(qa, n, &q) == 1 && q.Id[0] == '\0',
          "empty id (98 SE and ME) parsed, matched later by name and ids");
    {
        int byName = 7;
        CHECK(tr_query_matches(&q, QB_ID, 1, &byName) == 1 && byName == 1,
              "match: empty id, VEN/DEV equal, name unique");
        CHECK(tr_query_matches(&q, QB_ID, 0, &byName) == 0,
              "match: empty id, the name not unique");
        CHECK(tr_query_matches(&q, "PCI\\VEN_8086&DEV_15BF&SUBSYS_22B017AA&REV_01\\0000083000",
                               1, &byName) == 0,
              "match: empty id, another device id");
        CHECK(tr_query_matches(&q, "PCI\\VEN_8087&DEV_15EB\\0", 1, &byName) == 0,
              "match: empty id, another vendor");
        CHECK(tr_query_matches(&q, "ROOT\\XHCICM\\0000", 1, &byName) == 0,
              "match: empty id, a devnode with no PCI ids");
    }
    put32(qa, CM_Q_O_ID_BYTES, (TR_U32)strlen(QB_ID));
    CHECK(tr_query_parse(qa, n, &q) == 1, "id back");
    {
        int byName = 7;
        CHECK(tr_query_matches(&q, QB_ID, 0, &byName) == 1 && byName == 0,
              "match: the instance id itself");
        CHECK(tr_query_matches(&q, "PCI\\VEN_8086&DEV_15EB&SUBSYS_00000000&REV_06\\OTHER",
                               1, &byName) == 0,
              "match: an id given and different, never by name");
    }
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
    CHECK(tr_query_parse(qa, n, &q) == 0, "version 2: a record inside the block");
    put32(qa, CM_Q_O_REC_OFFSET, 96UL);
    put32(qa, 96 + CM_ST_O_ENTRY_BYTES, 64UL);
    CHECK(tr_query_parse(qa, n, &q) == 0, "a record inst_st_check refuses");
    put32(qa, 96 + CM_ST_O_ENTRY_BYTES, CM_ST_ENTRY_BYTES);
    put32(qa, 96 + CM_ST_O_BYTES, 0x10000UL);
    CHECK(tr_query_parse(qa, n, &q) == 0, "a record bigger than the answer");
    put32(qa, 96 + CM_ST_O_BYTES, 128UL + 256UL);

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
    put32(qa, 96 + CM_ST_O_STAGE, CM_ST_STAGE_FAILED);
    CHECK(tr_query_eject_ready(&q, &why) == 0 && has(why, "not active"),
          "eject: stage not active");
    put32(qa, 96 + CM_ST_O_STAGE, CM_ST_STAGE_ACTIVE);
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
    put32(qb, 96 + CM_ST_O_GENERATION, 8UL);
    CHECK(tr_query_same_content(&q, &q2),
          "the record's generation alone is not content");
    put32(qb, 96 + CM_ST_O_GENERATION, 7UL);
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
    /* 15:8 is ROUTER_CS_4's USB4 Version byte: major 7:5, minor 4:0. */
    tr_gen_text(CM_ST_GEN_USB4 | 0x2000UL, out, sizeof(out));
    CHECK(strcmp(out, "USB4 v1.0") == 0, "gen USB4 v1.0 (20h)");
    tr_gen_text(CM_ST_GEN_USB4 | 0x2100UL, out, sizeof(out));
    CHECK(strcmp(out, "USB4 v1.1") == 0, "gen USB4 v1.1 (21h)");
    tr_gen_text(CM_ST_GEN_USB4 | 0x4000UL, out, sizeof(out));
    CHECK(strcmp(out, "USB4 v2.0") == 0, "gen USB4 v2.0 (40h)");
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
    memset(&x, 0, sizeof(x));
    x.Rate = 20;
    x.Width = 2;
    x.LinkState = CM_Q_UNKNOWN;
    x.GenRaw = CM_Q_UNKNOWN;
    tr_link_text(CM_ST_KIND_ICM, CM_ST_LANES_BONDED, &x, out, sizeof(out));
    CHECK(strcmp(out, "20 Gbit/s per lane, x2 (reported by the firmware)") == 0,
          "link: ICM rate and width from the firmware's event");
    x.Flags = CM_Q_XF_RATE_READ;
    tr_link_text(CM_ST_KIND_ICM, CM_ST_LANES_BONDED, &x, out, sizeof(out));
    CHECK(strcmp(out, "20 Gbit/s per lane, x2 (read from the lane adapter)") == 0,
          "link: ICM rate read from the lane adapter");
    x.Flags = 0;
    x.Width = 0;
    tr_link_text(CM_ST_KIND_ICM, CM_ST_LANES_UNKNOWN, &x, out, sizeof(out));
    CHECK(strcmp(out, "20 Gbit/s per lane, lanes not known (reported by the firmware)") == 0,
          "link: ICM rate without width");
    x.Rate = 0;
    x.Width = 2;
    tr_link_text(CM_ST_KIND_ICM, CM_ST_LANES_BONDED, &x, out, sizeof(out));
    CHECK(has(out, "not reported by the firmware"), "link: ICM rate 0");
    x.Rate = 20;
    x.Flags = CM_Q_XF_RATE_READ;
    tr_link_text(CM_ST_KIND_USB4_PORT, CM_ST_LANES_BONDED, &x, out, sizeof(out));
    CHECK(strcmp(out, "USB4 Gen 3 x2 (40 Gbit/s) (read from the lane adapter)") == 0,
          "link: USB4 rate read from the lane adapter");
    tr_link_text(CM_ST_KIND_USB4_PORT, CM_ST_LANES_BONDED, NULL, out, sizeof(out));
    CHECK(strcmp(out, "two lanes; speed not known") == 0, "link: two lanes only");

    /* A Thunderbolt 3 router behind a USB4 host (task 40-A.8). */
    CHECK(tr_gen_tbt3(CM_ST_GEN_TBT3), "TBT3 by the generation");
    CHECK(tr_gen_tbt3(CM_ST_GEN_USB4 | 0x1000UL), "USB4 Version 10h: major 0");
    CHECK(tr_gen_tbt3(CM_ST_GEN_USB4 | 0x0200UL), "USB4 Version 02h: major 0");
    CHECK(!tr_gen_tbt3(CM_ST_GEN_USB4 | 0x2000UL), "USB4 v1.0 is not TBT3");
    CHECK(!tr_gen_tbt3(CM_ST_GEN_USB4 | 0x4000UL), "USB4 v2.0 is not TBT3");
    CHECK(!tr_gen_tbt3(CM_ST_GEN_USB4), "USB4 without a version");
    CHECK(!tr_gen_tbt3(CM_ST_GEN_UNKNOWN), "unknown");
    memset(&x, 0, sizeof(x));
    x.Rate = 20;
    x.Width = 2;
    x.Flags = CM_Q_XF_RATE_READ;
    tr_link_text_gen(CM_ST_GEN_USB4 | 0x1000UL, CM_ST_KIND_USB4_LINK,
                     CM_ST_LANES_BONDED, &x, out, sizeof(out));
    CHECK(strcmp(out, "Thunderbolt 3, 20 Gbit/s per lane, x2 (40 Gbit/s) "
                      "(read from the lane adapter)") == 0,
          "link: a TBT3 router behind a USB4 host, never a USB4 generation");
    tr_link_text_gen(CM_ST_GEN_TBT3, CM_ST_KIND_USB4_LINK, CM_ST_LANES_BONDED,
                     &x, out, sizeof(out));
    CHECK(has(out, "Thunderbolt 3, 20 Gbit/s per lane") && !has(out, "Gen"),
          "link: TBT3 by the generation");
    x.Width = 0;
    x.Flags = 0;
    tr_link_text_gen(CM_ST_GEN_TBT3, CM_ST_KIND_USB4_PORT, CM_ST_LANES_UNKNOWN,
                     &x, out, sizeof(out));
    CHECK(strcmp(out, "Thunderbolt 3, 20 Gbit/s per lane, lanes not known") == 0,
          "link: TBT3 without width");
    x.Width = 2;
    x.Flags = CM_Q_XF_RATE_READ;
    tr_link_text_gen(CM_ST_GEN_USB4 | 0x2000UL, CM_ST_KIND_USB4_PORT,
                     CM_ST_LANES_BONDED, &x, out, sizeof(out));
    CHECK(strcmp(out, "USB4 Gen 3 x2 (40 Gbit/s) (read from the lane adapter)") == 0,
          "link: a USB4 v1.0 router keeps its generation");
    tr_link_text_gen(CM_ST_GEN_TBT3, CM_ST_KIND_ICM, CM_ST_LANES_BONDED, &x,
                     out, sizeof(out));
    CHECK(strcmp(out, "20 Gbit/s per lane, x2 (read from the lane adapter)") == 0,
          "link: an ICM entry as before");
    tr_link_text_gen(CM_ST_GEN_TBT3, CM_ST_KIND_USB4_LINK, CM_ST_LANES_BONDED,
                     NULL, out, sizeof(out));
    CHECK(strcmp(out, "two lanes; speed not known") == 0,
          "link: TBT3 without an extension as before");

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
static TR_SNAP s3;

/* Design record 25 section 7.6: the removal block's readers, the event and
 * the details line. */
static void test_removals(void)
{
    TR_EVENT ev[8];
    TR_QUERY q;
    MK_QUERY mq;
    INST_U32 need;
    char out[160];
    size_t n;
    int k;

    /* Version 2. */
    qbRmFlags = CM_Q_RF_WATCHED;
    qbRmCount = 3UL;
    qbRmMember = 0x0E21UL;
    n = qb_build(qa, 1UL, 0x11111111UL, 5UL);
    CHECK(tr_query_parse(qa, n, &q) == 1, "v2 parses");
    CHECK_EQ(q.Version, 2, "v2: version");
    CHECK_EQ(q.RmFlags, CM_Q_RF_WATCHED, "v2: watched");
    CHECK_EQ(q.RmCount, 3, "v2: count");
    CHECK_EQ(q.RmMember, 0x0E21, "v2: member");
    CHECK(q.Rec == qa + 96, "v2: the record at 96");
    tr_removal_text(&q, out, sizeof(out));
    CHECK(strcmp(out, "3 since this start; the last on tunnel port 0E:04.1")
              == 0, "text: count and port");
    put32(qa, CM_Q_HDR_BYTES + CM_Q_RM_LOST, 2UL);
    (void)tr_query_parse(qa, n, &q);
    tr_removal_text(&q, out, sizeof(out));
    CHECK(has(out, "; 2 in a loss of the controller"), "text: the loss");
    CHECK(mk_query_parse(qa, n, &mq, &need) == 1, "the mark service takes v2");
    put32(qa, CM_Q_O_REC_OFFSET, 80UL);
    CHECK(tr_query_parse(qa, n, &q) == 0, "v2: a record inside the block");
    qbRmCount = 0UL;
    qbRmMember = CM_Q_UNKNOWN;
    n = qb_build(qa, 1UL, 0x11111111UL, 5UL);
    (void)tr_query_parse(qa, n, &q);
    tr_removal_text(&q, out, sizeof(out));
    CHECK(strcmp(out, "none since this start") == 0, "text: none");
    qbRmFlags = 0UL;
    n = qb_build(qa, 1UL, 0x11111111UL, 5UL);
    (void)tr_query_parse(qa, n, &q);
    tr_removal_text(&q, out, sizeof(out));
    CHECK(strcmp(out, "not watched on this system") == 0, "text: not watched");

    /* Version 1: no block. */
    qbV1 = 1;
    n = qb_build(qa, 1UL, 0x11111111UL, 5UL);
    CHECK(tr_query_parse(qa, n, &q) == 1, "v1 still parses");
    CHECK_EQ(q.Version, 1, "v1: version");
    CHECK_EQ(q.RmFlags | q.RmCount, 0, "v1: no block");
    CHECK_EQ(q.RmMember, CM_Q_UNKNOWN, "v1: no member");
    CHECK(q.Rec == qa + 64, "v1: the record at 64");
    tr_removal_text(&q, out, sizeof(out));
    CHECK(strcmp(out, "not reported by this driver") == 0, "text: v1");
    CHECK(mk_query_parse(qa, n, &mq, &need) == 1, "the mark service takes v1");
    qbV1 = 0;

    /* The event: a watched NHI, both ACTIVE, the count moved. */
    qbRmFlags = CM_Q_RF_WATCHED;
    qbRmCount = 3UL;
    n = qb_build(qa, 1UL, 0x11111111UL, 5UL);
    snap_of(&so, qa, n, 1UL);
    CHECK_EQ(tr_diff(&so, &so, ev, 8), 0, "no change: none");
    qbRmCount = 4UL;
    n = qb_build(qb, 1UL, 0x11111111UL, 6UL);
    snap_of(&sn, qb, n, 1UL);
    k = tr_diff(&so, &sn, ev, 8);
    CHECK_EQ(k, 1, "a rise: one event");
    CHECK_EQ(ev[0].Kind, TR_EV_REMOVED, "removed");
    tr_event_text(&ev[0], out, sizeof(out));
    CHECK(strcmp(out, "A Thunderbolt or USB4 device was removed unexpectedly.")
              == 0, "the text names no unit");
    CHECK(strcmp(tr_event_title(TR_EV_REMOVED), "Thunderbolt/USB4 warning") == 0,
          "a warning");
    CHECK(!has(out, "data") && !has(out, "unplug"), "no data, no unplug");
    CHECK_EQ(tr_diff(&so, &sn, ev, 0), 0, "cap 0");
    qbRmCount = 7UL;
    n = qb_build(qb, 1UL, 0x11111111UL, 6UL);
    snap_of(&sn, qb, n, 1UL);
    CHECK_EQ(tr_diff(&so, &sn, ev, 8), 1, "a rise of four: one event");
    qbRmCount = 0xFFFFFFFFUL;
    n = qb_build(qa, 1UL, 0x11111111UL, 5UL);
    snap_of(&so, qa, n, 1UL);
    qbRmCount = 0UL;
    n = qb_build(qb, 1UL, 0x11111111UL, 6UL);
    snap_of(&sn, qb, n, 1UL);
    CHECK_EQ(tr_diff(&so, &sn, ev, 8), 1, "a wrap: one event");
    /* Another start: none. */
    qbRmCount = 1UL;
    n = qb_build(qb, 1UL, 0x22222222UL, 1UL);
    snap_of(&sn, qb, n, 1UL);
    CHECK_EQ(tr_diff(&so, &sn, ev, 8), 0, "a new epoch: none");
    /* The liveness, either side (cm_live.h: 1 ACTIVE, 5 RECOVERING,
     * 7 CONTAINED). */
    n = qb_build(qb, 1UL, 0x11111111UL, 6UL);
    snap_of(&sn, qb, n, 7UL);
    k = tr_diff(&so, &sn, ev, 8);
    CHECK(k == 1 && ev[0].Kind == TR_EV_STOPPED,
          "left ACTIVE: the stop event alone");
    snap_of(&so, qa, qb_build(qa, 1UL, 0x11111111UL, 5UL), 5UL);
    snap_of(&sn, qb, n, 1UL);
    CHECK_EQ(tr_diff(&so, &sn, ev, 8), 0, "not ACTIVE before: none");
    /* Not watched on either side: none. */
    qbRmFlags = 0UL;
    qbRmCount = 5UL;
    snap_of(&so, qa, qb_build(qa, 1UL, 0x11111111UL, 5UL), 1UL);
    qbRmFlags = CM_Q_RF_WATCHED;
    qbRmCount = 6UL;
    snap_of(&sn, qb, qb_build(qb, 1UL, 0x11111111UL, 6UL), 1UL);
    CHECK_EQ(tr_diff(&so, &sn, ev, 8), 0, "not watched before: none");
    CHECK_EQ(tr_diff(&sn, &so, ev, 8), 0, "not watched now: none");
    /* Version 1 before: none. */
    qbV1 = 1;
    snap_of(&so, qa, qb_build(qa, 1UL, 0x11111111UL, 5UL), 1UL);
    qbV1 = 0;
    CHECK_EQ(tr_diff(&so, &sn, ev, 8), 0, "a v1 answer before: none");
    /* A full buffer: the warning comes first. */
    qbRmCount = 1UL;
    n = qb_build(qa, 0UL, 0x11111111UL, 5UL);
    snap_of(&so, qa, n, 1UL);
    qbRmCount = 2UL;
    n = qb_build(qb, 3UL, 0x11111111UL, 6UL);
    qb_entry(qb, 0UL, CM_ST_KIND_USB4_PORT, 0x0101UL, 1UL,
             CM_ST_APPR_APPROVED, CM_ST_TUN_UP, CM_ST_GEN_USB4, "A", "1");
    qb_entry(qb, 1UL, CM_ST_KIND_USB4_PORT, 0x0102UL, 2UL,
             CM_ST_APPR_APPROVED, CM_ST_TUN_UP, CM_ST_GEN_USB4, "B", "2");
    qb_entry(qb, 2UL, CM_ST_KIND_USB4_PORT, 0x0103UL, 3UL,
             CM_ST_APPR_APPROVED, CM_ST_TUN_UP, CM_ST_GEN_USB4, "C", "3");
    snap_of(&sn, qb, n, 1UL);
    k = tr_diff(&so, &sn, ev, 2);
    CHECK_EQ(k, 2, "a full buffer");
    CHECK_EQ(ev[0].Kind, TR_EV_REMOVED, "the warning not dropped");

    /* Beside a departure, which is shown as before. */
    qbRmCount = 1UL;
    n = qb_build(qa, 1UL, 0x11111111UL, 5UL);
    qb_entry(qa, 0UL, CM_ST_KIND_USB4_PORT, 0x0101UL, 1UL, CM_ST_APPR_APPROVED,
             CM_ST_TUN_UP, CM_ST_GEN_USB4 | 0x2000UL, "Acme", "Dock");
    snap_of(&so, qa, n, 1UL);
    qbRmCount = 2UL;
    snap_of(&sn, qb, qb_build(qb, 0UL, 0x11111111UL, 6UL), 1UL);
    k = tr_diff(&so, &sn, ev, 8);
    CHECK_EQ(k, 2, "a departure and a removal");
    CHECK_EQ(ev[0].Kind, TR_EV_REMOVED, "the removal first");
    CHECK(ev[1].Kind == TR_EV_LEFT && strcmp(ev[1].Name, "Acme Dock") == 0,
          "the unit's disconnected, named");
    /* One balloon for both (T.11, 2026-10-22: on XP the second balloon
     * replaced the warning at once). */
    {
        char all[256];
        char small[80];
        CHECK_EQ(tr_events_text(ev, k, all, sizeof(all)), 2, "one balloon: two lines");
        CHECK(strcmp(all, "A Thunderbolt or USB4 device was removed unexpectedly.\n"
                          "Acme Dock was disconnected.") == 0,
              "one balloon: the warning first, the departure below it");
        CHECK_EQ(tr_events_text(ev, k, small, sizeof(small)), 1,
                 "one balloon: a line that does not fit is left out");
        CHECK(strcmp(small, "A Thunderbolt or USB4 device was removed unexpectedly.") == 0,
              "one balloon: the warning whole");
        CHECK_EQ(tr_events_text(ev, k, small, 10), 0, "one balloon: nothing fits");
        CHECK(small[0] == '\0', "one balloon: nothing fits - empty");
        CHECK_EQ(tr_events_text(ev, 0, all, sizeof(all)), 0, "one balloon: no events");
    }

    /* The option: only a DWORD 0 turns the warning off. */
    CHECK(tr_warn_removed(0, 0UL), "option: no value - on");
    CHECK(tr_warn_removed(1, 1UL), "option: 1 - on");
    CHECK(tr_warn_removed(1, 7UL), "option: another number - on");
    CHECK(!tr_warn_removed(1, 0UL), "option: a DWORD 0 - off");
    CHECK(strcmp(TR_WARN_VALUE, "WarnRemoved") == 0, "option: its name");

    qbRmFlags = 0UL;
    qbRmCount = 0UL;
    qbRmMember = CM_Q_UNKNOWN;
}

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
             CM_ST_TUN_UP, CM_ST_GEN_USB4 | 0x2000UL, "Acme", "Dock");
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
    CHECK_EQ(so.R[0].Gen, CM_ST_GEN_USB4 | 0x2000UL, "snap: gen");
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
             CM_ST_TUN_UP, CM_ST_GEN_USB4 | 0x2000UL, "Acme", "Dock");
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

    /* A sleep: ACTIVE, then OFF ("not monitored", CmLiveDisarm), then
     * ACTIVE after the wake. Nothing failed, so nothing is raised. */
    snap_of(&so, qa, n, 1UL);
    snap_of(&sn, qa, n, 0UL);
    snap_of(&s3, qa, n, 1UL);
    CHECK_EQ(tr_diff(&so, &sn, ev, 8), 0, "diff: ACTIVE to OFF (a sleep) is quiet");
    CHECK_EQ(tr_diff(&sn, &s3, ev, 8), 0, "diff: OFF to ACTIVE (the wake) is quiet");
    /* ACTIVE to each loss state raises it, whatever came before. */
    {
        static const TR_U32 lost[] = { 2UL, 3UL, 4UL, 5UL, 6UL, 7UL, 9UL };
        size_t j;
        for (j = 0; j < sizeof(lost) / sizeof(lost[0]); j++) {
            snap_of(&sn, qa, n, lost[j]);
            k = tr_diff(&s3, &sn, ev, 8);
            CHECK(k == 1 && ev[0].Kind == TR_EV_STOPPED,
                  "diff: ACTIVE (after a wake) to a loss state is STOPPED");
            CHECK_EQ(tr_live_lost(lost[j]), 1, "a loss state");
        }
    }
    /* Neither a stop's revocation nor a state not known is a loss. */
    snap_of(&sn, qa, n, 8UL);
    CHECK_EQ(tr_diff(&s3, &sn, ev, 8), 0, "diff: ACTIVE to REVOKED is quiet");
    snap_of(&sn, qa, n, 10UL);
    CHECK_EQ(tr_diff(&s3, &sn, ev, 8), 0, "diff: ACTIVE to an unknown state is quiet");
    CHECK_EQ(tr_live_lost(0UL), 0, "OFF is no loss");
    CHECK_EQ(tr_live_lost(1UL), 0, "ACTIVE is no loss");
    CHECK_EQ(tr_live_lost(8UL), 0, "REVOKED is no loss");
    /* A loss that persists is raised once. */
    snap_of(&so, qa, n, 2UL);
    snap_of(&sn, qa, n, 3UL);
    CHECK_EQ(tr_diff(&so, &sn, ev, 8), 0, "diff: SUSPECT to ABSENT is quiet");
    /* A resume that failed: the liveness stays OFF, the stage goes to
     * failed (cm_power.c). Raised once, whether or not the sleep's OFF was
     * sampled in between; a healthy resume (above) stays quiet. */
    snap_of(&so, qa, n, 1UL);
    CHECK_EQ(so.Stage[0], CM_ST_STAGE_ACTIVE, "snap: its stage");
    snap_of(&sn, qa, n, 0UL);
    sn.Stage[0] = CM_ST_STAGE_FAILED;
    k = tr_diff(&so, &sn, ev, 8);
    CHECK(k == 1 && ev[0].Kind == TR_EV_STOPPED,
          "diff: ACTIVE to OFF with the stage failed is STOPPED");
    snap_of(&s3, qa, n, 0UL);
    k = tr_diff(&s3, &sn, ev, 8);
    CHECK(k == 1 && ev[0].Kind == TR_EV_STOPPED,
          "diff: OFF (asleep) to OFF with the stage failed is STOPPED");
    CHECK_EQ(tr_diff(&sn, &sn, ev, 8), 0, "diff: a failed stage stays quiet");
    snap_of(&s3, qa, n, 2UL);
    s3.Stage[0] = CM_ST_STAGE_FAILED;
    CHECK_EQ(tr_diff(&so, &s3, ev, 8), 1, "diff: a loss and a failed stage, one event");

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

/* A descriptor whose own length claims more than the bytes read (review
 * of 2026-10-16): refused, never dumped past the buffer. */
static void test_desc_lengths(void)
{
    static const unsigned char bos[5] = { 255, 15, 5, 0, 0 };
    static const unsigned char cfg[9] = { 200, 2, 9, 0, 1, 1, 0, 0x80, 50 };
    static const unsigned char hub[7] = { 71, 0x29, 4, 0, 0, 50, 0 };
    char buf[2048];
    char ssp[64];
    TR_TEXT t;

    tr_text_init(&t, buf, sizeof(buf));
    tr_dec_bos(&t, bos, sizeof(bos));
    CHECK(has(buf, "short or malformed"), "BOS bLength 255 of 5 bytes refused");
    CHECK(!tr_bos_ssp_text(bos, sizeof(bos), ssp, sizeof(ssp)),
          "BOS bLength 255: no sublink text");
    tr_text_init(&t, buf, sizeof(buf));
    tr_dec_config(&t, cfg, sizeof(cfg));
    CHECK(has(buf, "short or malformed"), "config bLength 200 of 9 bytes refused");
    {
        static const unsigned char cfg2[9] = { 2, 2, 9, 0, 1, 1, 0, 0x80, 50 };
        static const unsigned char dev255[18] = {
            255, 1, 0, 2, 0, 0, 0, 64, 0x81, 0x07, 0x81, 0x55, 0, 1, 1, 2, 3, 1
        };
        unsigned int ma = 0;
        int self = 0;
        CHECK(!tr_config_power(cfg, sizeof(cfg), TR_SPEED_HIGH, &ma, &self),
              "power: bLength 200 of 9 bytes refused");
        CHECK(!tr_config_power(cfg2, sizeof(cfg2), TR_SPEED_HIGH, &ma, &self),
              "power: bLength 2 refused");
        tr_text_init(&t, buf, sizeof(buf));
        tr_dec_device(&t, dev255, sizeof(dev255));
        CHECK(has(buf, "short or not a device descriptor"),
              "device bLength 255 of 18 bytes refused");
    }
    tr_text_init(&t, buf, sizeof(buf));
    tr_dec_hub(&t, hub, sizeof(hub));
    CHECK(has(buf, "short or not reported"), "hub bLength 71 of 7 bytes refused");
}

/* ---- plain USB devices (record 25 section 6.5, revision 7) ------------- */

/* A device that is offered: started, no problem, its set known and read. */
static void usbf_base(TR_USBF *f, int nt)
{
    memset(f, 0, sizeof(*f));
    f->Nt = nt;
    f->HaveDevnode = 1;
    f->Started = 1;
    f->SetKnown = 1;
    f->SetCount = 3;
    f->ReadWhole = 1;
    f->CallAvailable = 1;
}

static void test_usb_eject(void)
{
    static const char ids[] = "USB\\VID_0781&PID_5581&REV_0100\0USB\\COMPOSITE\0";
    static const char compatStor[] = "USB\\Class_08&SubClass_06&Prot_50\0USB\\Class_08\0";
    static const char compatHid[] = "USB\\Class_03&SubClass_01&Prot_02\0usb\\class_03\0";
    static const char compatHub[] = "USB\\Class_09&SubClass_00\0USB\\Class_09\0";
    TR_USBF f;
    const char *why;
    char out[512];
    char wide[400];
    int r;

    /* the ids */
    CHECK(tr_msz_has_prefix(compatStor, sizeof(compatStor), "usb\\class_08"),
          "msz prefix: without case");
    CHECK(!tr_msz_has_prefix(compatStor, sizeof(compatStor), "USB\\Class_03"),
          "msz prefix: storage is not input");
    CHECK(!tr_msz_has_prefix(compatStor, 10, "USB\\Class_08"),
          "msz prefix: an id the buffer cuts short is not read");
    CHECK(!tr_msz_has_prefix(NULL, 0, "USB"), "msz prefix: no list");
    CHECK(tr_msz_has_prefix(ids, sizeof(ids), "usb\\composite"),
          "msz prefix: the second id");

    CHECK(tr_node_is_hid(NULL, ids, sizeof(ids), compatHid, sizeof(compatHid),
                         "", ""), "hid: a USB\\Class_03 compatible id");
    CHECK(tr_node_is_hid("HID\\VID_0627&PID_0001\\7&1", NULL, 0, NULL, 0, NULL,
                         NULL), "hid: an HID\\ instance id");
    CHECK(tr_node_is_hid("USB\\VID_1\\2", NULL, 0, NULL, 0, "HidUsb", NULL),
          "hid: bound to hidusb");
    CHECK(tr_node_is_hid(NULL, NULL, 0, NULL, 0, NULL, "Keyboard") &&
          tr_node_is_hid(NULL, NULL, 0, NULL, 0, NULL, "mouse") &&
          tr_node_is_hid(NULL, NULL, 0, NULL, 0, NULL, "HIDClass"),
          "hid: the three setup classes");
    CHECK(!tr_node_is_hid("USB\\VID_0781&PID_5581\\0123", ids, sizeof(ids),
                          compatStor, sizeof(compatStor), "USBSTOR", "USB"),
          "hid: a storage stick is not input");
    CHECK(tr_node_is_hub(NULL, 0, compatHub, sizeof(compatHub)),
          "hub: USB\\Class_09");
    CHECK(!tr_node_is_hub(ids, sizeof(ids), compatStor, sizeof(compatStor)),
          "hub: a stick is not one");

    /* eligibility: NT. Every xhci98 device says removable, an internal
     * camera, fingerprint reader or radio too: a disk or volume decides. */
    usbf_base(&f, 1);
    CHECK(tr_usb_rule(&f, &why) == TR_UE_DETAILS &&
          strcmp(why, "no disk on it") == 0,
          "rule NT: removable alone (a camera, a fingerprint reader) is not offered");
    f.Disks = 1;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_OFFER && why == NULL,
          "rule NT: a disk under it");
    f.Disks = 0;
    f.Volumes = 1;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_OFFER, "rule NT: a volume in its set");
    f.Radio = 1;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_DETAILS &&
          strcmp(why, "a wireless radio") == 0, "rule NT: class E0 never");

    /* eligibility: 98 SE and ME */
    usbf_base(&f, 0);
    f.Volumes = 1;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_DETAILS &&
          strcmp(why, "no disk on it") == 0,
          "rule 9x: no disk, no offer (volumes are an NT reading)");
    f.Disks = 2;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_OFFER, "rule 9x: disks under it");
    f.HaveDevnode = 0;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_DETAILS,
          "rule 9x: a device with no driver's devnode");

    /* never listed */
    usbf_base(&f, 1);
    f.Disks = 1;
    f.IsHub = 1;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_DETAILS && strcmp(why, "a hub") == 0,
          "rule: a hub, even with a disk behind it");
    f.IsHub = 0;
    f.Hid = 1;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_DETAILS &&
          strcmp(why, "an input device") == 0,
          "rule: input (a keyboard, or a composite with an input function)");
    f.Hid = 0;
    f.IsController = 1;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_DETAILS, "rule: a controller");
    f.IsController = 0;
    f.HaveDevnode = 0;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_DETAILS,
          "rule: a port with no device entry");

    /* listed, not offered */
    usbf_base(&f, 1);
    f.Disks = 1;
    f.Problem = 47;
    f.Started = 0;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_LISTED &&
          strcmp(why, "stopped - unplug it") == 0, "rule: Code 47");
    f.Disks = 0;
    f.Volumes = 0;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_LISTED &&
          strcmp(why, "stopped - unplug it") == 0,
          "rule: Code 47 after its disk went with the eject (XP)");
    f.Hid = 1;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_DETAILS,
          "rule: an input device held for eject is still not listed");
    f.Hid = 0;
    f.Problem = 24;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_DETAILS &&
          strcmp(why, "no disk on it") == 0,
          "rule: Code 24 with no disk is not held for eject outside 2000");
    f.W2k = 1;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_LISTED &&
          strcmp(why, "stopped - unplug it") == 0,
          "rule: Code 24 after its disk went with the stop (2000)");
    f.W2k = 0;
    f.Disks = 1;
    f.Problem = 28;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_LISTED &&
          strcmp(why, "Windows could not start it") == 0,
          "rule: another problem code");
    f.Problem = 0;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_LISTED, "rule: not started");
    f.Started = 1;
    f.SetKnown = 0;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_LISTED, "rule: its set not known");
    f.SetKnown = 1;
    f.SetCount = TR_USB_SET_MAX + 1;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_LISTED, "rule: a set too long to list");
    f.SetCount = TR_USB_SET_MAX;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_OFFER, "rule: 64 devnodes are listed whole");
    f.ReadWhole = 0;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_LISTED, "rule: read in part");
    f.ReadWhole = 1;
    f.CallAvailable = 0;
    CHECK(tr_usb_rule(&f, &why) == TR_UE_LISTED, "rule: no routine");

    /* the words for a devnode that is not running: only held for eject
     * (Code 47, or 24 on 2000 after an accepted stop) says unplug */
    {
        char tag[64];
        char sen[96];
        CHECK(tr_stopped_text(47, 0, tag, sizeof(tag), sen, sizeof(sen)) == 1 &&
              strcmp(tag, "stopped - unplug it") == 0 &&
              strcmp(sen, "Stopped: you can unplug it.") == 0, "stopped: Code 47");
        CHECK(tr_stopped_text(29, 0, tag, sizeof(tag), sen, sizeof(sen)) == 0 &&
              strcmp(tag, "not started - problem code 29") == 0 &&
              strcmp(sen, "Windows could not start this device (problem code 29).") == 0 &&
              !has(tag, "unplug") && !has(sen, "unplug"),
              "stopped: Code 29 is a failed start, never 'unplug it'");
        CHECK(tr_stopped_text(28, 0, tag, sizeof(tag), sen, sizeof(sen)) == 0 &&
              has(tag, "code 28"), "stopped: Code 28, no driver");
        CHECK(tr_stopped_text(0, 0, tag, sizeof(tag), sen, sizeof(sen)) == 0 &&
              strcmp(tag, "not started") == 0 && !has(sen, "unplug"),
              "stopped: not started, no problem code");
        CHECK(tr_stopped_text(24, 1, tag, sizeof(tag), sen, sizeof(sen)) == 1 &&
              strcmp(tag, "stopped - unplug it") == 0 &&
              strcmp(sen, "Stopped: you can unplug it.") == 0,
              "stopped: Code 24 on 2000 is held for eject");
        CHECK(tr_stopped_text(24, 0, tag, sizeof(tag), sen, sizeof(sen)) == 0 &&
              has(tag, "code 24") && !has(sen, "unplug"),
              "stopped: Code 24 elsewhere is not");
        CHECK(tr_stopped_text(47, 1, tag, sizeof(tag), sen, sizeof(sen)) == 1 &&
              tr_stopped_text(28, 1, tag, sizeof(tag), sen, sizeof(sen)) == 0,
              "stopped: 2000 keeps the other codes");
        CHECK(tr_w2k_code24(1, 1, 1, 1) && tr_w2k_code24(1, 1, 0, 0),
              "code 24: 2000, stopped here, still configured (or a unit)");
        CHECK(!tr_w2k_code24(1, 1, 1, 0),
              "code 24: plugged in again and failed to start, unconfigured");
        CHECK(!tr_w2k_code24(1, 0, 1, 1) && !tr_w2k_code24(1, 0, 0, 0),
              "code 24: not stopped here (a failed start, or another tool)");
        CHECK(!tr_w2k_code24(0, 1, 1, 1), "code 24: not 2000");
    }

    /* what a walk right after an accepted stop reads (section 6.5 step 5) */
    CHECK(tr_usb_after_kind(0, 0, 0, 0, 0, 0) == TR_UA_UNKNOWN &&
          tr_usb_after_kind(0, 0, 0, 0, 0, 1) == TR_UA_UNKNOWN,
          "after: no walk is not known");
    CHECK(tr_usb_after_kind(1, 0, 0, 0, 0, 0) == TR_UA_UNPLUG &&
          tr_usb_after_kind(1, 0, 0, 0, 0, 1) == TR_UA_UNPLUG,
          "after: gone from a whole walk");
    CHECK(tr_usb_after_kind(1, 0, 2, 0, 0, 0) == TR_UA_UNKNOWN &&
          tr_usb_after_kind(1, 0, 2, 0, 0, 1) == TR_UA_UNKNOWN,
          "after: gone from a walk read in part is not known");
    CHECK(tr_usb_after_kind(1, 1, 0, 1, 0, 0) == TR_UA_STARTED &&
          tr_usb_after_kind(1, 1, 0, 1, 0, 1) == TR_UA_STARTED &&
          tr_usb_after_kind(1, 1, 3, 1, 0, 1) == TR_UA_STARTED,
          "after: started again");
    CHECK(tr_usb_after_kind(1, 1, 0, 0, 47, 0) == TR_UA_UNPLUG &&
          tr_usb_after_kind(1, 1, 0, 0, 47, 1) == TR_UA_UNPLUG,
          "after: Code 47, held for eject");
    CHECK(tr_usb_after_kind(1, 1, 0, 0, 24, 1) == TR_UA_UNPLUG &&
          tr_usb_after_kind(1, 1, 1, 0, 24, 1) == TR_UA_UNPLUG,
          "after: Code 24 on 2000, removed and still attached");
    CHECK(tr_usb_after_kind(1, 1, 0, 0, 24, 0) == TR_UA_UNKNOWN,
          "after: Code 24 on any other system is not known");
    CHECK(tr_usb_after_kind(1, 1, 0, 0, 0, 1) == TR_UA_UNKNOWN &&
          tr_usb_after_kind(1, 1, 0, 0, 29, 1) == TR_UA_UNKNOWN &&
          tr_usb_after_kind(1, 1, 0, 0, 21, 1) == TR_UA_UNKNOWN &&
          tr_usb_after_kind(1, 1, 0, 0, 0, 0) == TR_UA_UNKNOWN &&
          tr_usb_after_kind(1, 1, 0, 0, 29, 0) == TR_UA_UNKNOWN,
          "after: not started with no code or another code is not known");

    /* the check before the call */
    {
        const char *then[3];
        const char *now[3];
        then[0] = "USB\\VID_0781&PID_5581\\0123";
        then[1] = "USBSTOR\\DISK&VEN_X\\0123&0";
        then[2] = "STORAGE\\VOLUME\\1&30A9";
        now[0] = "storage\\volume\\1&30a9";
        now[1] = "USB\\VID_0781&PID_5581\\0123";
        now[2] = "USBSTOR\\DISK&VEN_X\\0123&0";
        usbf_base(&f, 1);
        f.Disks = 1;
        CHECK(tr_usb_recheck("USB\\ROOT_HUB30\\4&1", "usb\\root_hub30\\4&1", now,
                             3, then, 3, &f, &why),
              "recheck: the same parent and set, any order and case");
        CHECK(!tr_usb_recheck("USB\\VID_05E3&PID_0610\\5&2", "USB\\ROOT_HUB30\\4&1",
                              now, 3, then, 3, &f, &why) &&
              strcmp(why, "it is no longer where it was") == 0,
              "recheck: moved behind a hub");
        CHECK(!tr_usb_recheck("A", "", now, 3, then, 3, &f, &why),
              "recheck: no parent confirmed");
        CHECK(!tr_usb_recheck("P", "P", now, 2, then, 3, &f, &why) &&
              strcmp(why, "the devices on it changed") == 0,
              "recheck: a devnode gone");
        now[2] = "USBSTOR\\DISK&VEN_X\\0123&1";
        CHECK(!tr_usb_recheck("P", "P", now, 3, then, 3, &f, &why),
              "recheck: a devnode replaced");
        now[2] = now[1];
        CHECK(!tr_usb_recheck("P", "P", now, 3, then, 3, &f, &why),
              "recheck: a repeated devnode does not stand for a missing one");
        now[2] = "USBSTOR\\DISK&VEN_X\\0123&0";
        f.Problem = 47;
        f.Started = 0;
        CHECK(!tr_usb_recheck("P", "P", now, 3, then, 3, &f, &why) &&
              strcmp(why, "stopped - unplug it") == 0,
              "recheck: stopped meanwhile");
        f.Problem = 0;
        f.Started = 1;
        f.Hid = 1;
        CHECK(!tr_usb_recheck("P", "P", now, 3, then, 3, &f, &why),
              "recheck: an input function now");
    }

    /* the answers */
    r = tr_usb_answer_nt(0, 0, "", 0, "F:", out, sizeof(out));
    CHECK(r == INST_EJ_DONE && out[0] == '\0', "answer NT: stopped");
    r = tr_usb_answer_nt(0, 5, "", 0, "F:", out, sizeof(out));
    CHECK(r == INST_EJ_VETO && has(out, "a program is using F:"),
          "answer NT: success with a veto type is a veto (XP)");
    CHECK(tr_eject_cr(0, 0) == 0 && tr_eject_cr(0, 6) == INST_CR_REMOVE_VETOED &&
          tr_eject_cr(INST_CR_ACCESS_DENIED, 12) == INST_CR_ACCESS_DENIED,
          "eject cr: only a success with a veto type changes");
    r = tr_usb_answer_nt(INST_CR_REMOVE_VETOED, 5, "STORAGE\\VOLUME\\1", 0, "F:",
                         out, sizeof(out));
    CHECK(r == INST_EJ_VETO &&
          strcmp(out, "Windows refused: a program is using F:. Close it and "
                      "try again.") == 0, "answer NT: an open file on F:");
    r = tr_usb_answer_nt(INST_CR_REMOVE_VETOED, 5, "", 0, "USB Mass Storage Device",
                         out, sizeof(out));
    CHECK(r == INST_EJ_VETO && has(out, "is using USB Mass Storage Device."),
          "answer NT: no letter, the name");
    r = tr_usb_answer_nt(INST_CR_REMOVE_VETOED, 3, "notepad.exe", 0, "F:, G:",
                         out, sizeof(out));
    CHECK(r == INST_EJ_VETO &&
          strcmp(out, "Windows refused: notepad.exe is using F:, G:. Close it "
                      "and try again.") == 0, "answer NT: an application");
    r = tr_usb_answer_nt(INST_CR_REMOVE_VETOED, 4, "", 0, "F:", out, sizeof(out));
    CHECK(r == INST_EJ_VETO && has(out, "a program is using F:"),
          "answer NT: a service with no name");
    r = tr_usb_answer_nt(INST_CR_REMOVE_VETOED, 6, "USBSTOR\\DISK\\1", 0, "F:",
                         out, sizeof(out));
    CHECK(r == INST_EJ_VETO &&
          strcmp(out, "Windows refused: USBSTOR\\DISK\\1 did not allow it.") == 0,
          "answer NT: a device's veto, named");
    r = tr_usb_answer_nt(INST_CR_REMOVE_VETOED, 6, "STORAGE\\RemovableMedia\\7&1&RM", 1,
                         "F:", out, sizeof(out));
    CHECK(r == INST_EJ_VETO &&
          strcmp(out, "Windows refused: F: is in use (a program has a file or "
                      "folder open on it). Close it and try again.") == 0,
          "answer NT: its own volume's veto names the drive (XP)");
    r = tr_usb_answer_nt(0, 6, "STORAGE\\RemovableMedia\\7&1&RM", 1, "F:", out,
                         sizeof(out));
    CHECK(r == INST_EJ_VETO && has(out, "F: is in use"),
          "answer NT: the same when XP answers success with the veto");
    r = tr_usb_answer_nt(INST_CR_REMOVE_VETOED, 99, "", 0, "F:", out, sizeof(out));
    CHECK(r == INST_EJ_VETO && has(out, "(veto 99)"),
          "answer NT: an unknown veto, numbered");
    r = tr_usb_answer_nt(INST_CR_REMOVE_VETOED, 12, "", 0, "F:", out, sizeof(out));
    CHECK(r == INST_EJ_RIGHTS, "answer NT: the rights veto");
    r = tr_usb_answer_nt(INST_CR_ACCESS_DENIED, 0, "", 0, "F:", out, sizeof(out));
    CHECK(r == INST_EJ_RIGHTS && has(out, "administrator"), "answer NT: access denied");
    r = tr_usb_answer_nt(INST_CR_NO_SUCH_DEVNODE, 0, "", 0, "F:", out, sizeof(out));
    CHECK(r == INST_EJ_CHANGED, "answer NT: gone");
    r = tr_usb_answer_nt(0x1FUL, 0, "", 0, "F:", out, sizeof(out));
    CHECK(r == INST_EJ_FAILED && has(out, "(code 0x0000001F)"),
          "answer NT: anything else, with its code");
    r = tr_usb_answer_nt(INST_CR_REMOVE_VETOED, 5, "", 0, "F:", out, 8);
    CHECK(r == INST_EJ_VETO && strlen(out) == 7, "answer NT: a small buffer");

    r = tr_usb_answer9x(0, "E:", out, sizeof(out));
    CHECK(r == INST_EJ_DONE && out[0] == '\0', "answer 9x: accepted");
    r = tr_usb_answer9x(INST_CR_REMOVE_VETOED, "E:", out, sizeof(out));
    CHECK(r == INST_EJ_VETO &&
          strcmp(out, "Windows refused: E: is in use. A program may have a "
                      "file open on it, or a DOS box may be using it. Close "
                      "it and try again.") == 0, "answer 9x: refused, no cause");
    r = tr_usb_answer9x(INST_CR_NO_SUCH_DEVNODE, "E:", out, sizeof(out));
    CHECK(r == INST_EJ_CHANGED && !has(out, "code"), "answer 9x: gone");
    r = tr_usb_answer9x(INST_CR_CALL_NOT_IMPLEMENTED, "E:", out, sizeof(out));
    CHECK(r == INST_EJ_UNSUPPORTED, "answer 9x: not offered");
    r = tr_usb_answer9x(0x1FUL, "E:", out, sizeof(out));
    CHECK(r == INST_EJ_VETO && has(out, "(code 0x0000001F)"),
          "answer 9x: another refusal, with its code");

    /* console text */
    tr_wrap("Windows has stopped USB Flash Disk (F:). You can now unplug the "
            "device. Until you unplug it, Device Manager's Refresh can start "
            "it again.", wide, sizeof(wide), 40);
    CHECK(strcmp(wide, "Windows has stopped USB Flash Disk (F:).\r\n"
                       "You can now unplug the device. Until you\r\n"
                       "unplug it, Device Manager's Refresh can\r\n"
                       "start it again.") == 0, "wrap: at the last space");
    {
        const char *p = wide;
        int longest = 0;
        while (*p != '\0') {
            int n = 0;
            while (p[n] != '\0' && p[n] != '\r') {
                n++;
            }
            if (n > longest) {
                longest = n;
            }
            p += n;
            while (*p == '\r' || *p == '\n') {
                p++;
            }
        }
        CHECK(longest <= 40, "wrap: no line longer than the width");
    }
    tr_wrap("one\n\ntwo\r\n", wide, sizeof(wide), 79);
    CHECK(strcmp(wide, "one\r\n\r\ntwo\r\n") == 0, "wrap: breaks and paragraphs kept");
    tr_wrap("abcdefghijklmnop qr", wide, sizeof(wide), 8);
    CHECK(strcmp(wide, "abcdefgh\r\nijklmnop\r\nqr") == 0, "wrap: a long word cut");
    tr_wrap("aaaa bbbb cccc", wide, 7, 79);
    CHECK(strcmp(wide, "aaaa b") == 0, "wrap: cut short and terminated");
    tr_wrap("", wide, sizeof(wide), 79);
    CHECK(wide[0] == '\0', "wrap: nothing");
}

/* The elevated stop of a unit not marked removable (record 25 section
 * 6.6). */
static void test_elevated_stop(void)
{
    static char devs[3][TR_ID_CHARS];
    char buf[1024];
    char msg[512];
    char err[256];
    char d1[TR_DIGEST_CHARS];
    char d2[TR_DIGEST_CHARS];
    char *av[8];
    TR_OPTS o;
    int code;
    size_t n;

    /* When it is offered. */
    CHECK(tr_elev_needed(1, 1, 0), "NT, not marked, no administrator: offered");
    CHECK(!tr_elev_needed(1, 1, 1), "an administrator stops it here");
    CHECK(!tr_elev_needed(1, 0, 0), "a marked unit: the ordinary stop");
    CHECK(!tr_elev_needed(0, 1, 0), "98 SE and ME: never");
    CHECK(strcmp(tr_elev_question(), "Windows does not list this device for "
                 "safe removal. Stopping it needs administrator rights.") == 0,
          "the owner's words");

    /* The digest of the confirmed list. */
    strcpy(devs[0], "PCI\\VEN_1B21&DEV_2421\\4&1");
    strcpy(devs[1], "PCI\\VEN_1B36&DEV_000A\\5&2");
    strcpy(devs[2], "PCI\\VEN_1B36&DEV_0010\\6&3");
    tr_res_digest((const char (*)[TR_ID_CHARS])devs, 3, d1);
    CHECK(strlen(d1) == 16 && tr_res_digest_ok(d1), "16 hex digits");
    tr_res_digest((const char (*)[TR_ID_CHARS])devs, 3, d2);
    CHECK(strcmp(d1, d2) == 0, "the same list, the same digest");
    tr_res_digest((const char (*)[TR_ID_CHARS])devs, 2, d2);
    CHECK(strcmp(d1, d2) != 0, "a devnode more or less: another digest");
    devs[1][4] = 'v';
    tr_res_digest((const char (*)[TR_ID_CHARS])devs, 3, d2);
    devs[1][4] = 'V';
    CHECK(strcmp(d1, d2) == 0, "instance IDs compare without case");
    strcpy(devs[2], "PCI\\VEN_1B36&DEV_0010\\6&4");
    tr_res_digest((const char (*)[TR_ID_CHARS])devs, 3, d2);
    CHECK(strcmp(d1, d2) != 0, "another devnode: another digest");
    strcpy(devs[0], "AB");
    strcpy(devs[1], "C");
    tr_res_digest((const char (*)[TR_ID_CHARS])devs, 2, d1);
    strcpy(devs[0], "A");
    strcpy(devs[1], "BC");
    tr_res_digest((const char (*)[TR_ID_CHARS])devs, 2, d2);
    CHECK(strcmp(d1, d2) != 0, "the boundaries between IDs count");
    CHECK(!tr_res_digest_ok("0123456789ABCDE") && !tr_res_digest_ok("0123456789ABCDEF0") &&
              !tr_res_digest_ok("0123456789abcdef") && !tr_res_digest_ok(""),
          "a digest is exactly 16 upper-case hex digits");

    /* The section's name. */
    CHECK(tr_res_name_ok("Local\\Xhci98TrAnswer-1234-56789"), "the window's name");
    CHECK(!tr_res_name_ok("Local\\Xhci98TrAnswer-"), "no number");
    CHECK(!tr_res_name_ok("Global\\Xhci98TrAnswer-1"), "another namespace");
    CHECK(!tr_res_name_ok("Local\\Xhci98TrAnswer-1\\x"), "no path");
    CHECK(!tr_res_name_ok("Local\\Xhci98TrAnswer-1234567890123456789012345"),
          "bounded");
    CHECK(!tr_res_name_ok("C:\\TEMP\\x.tmp"), "never a file");

    /* The command line of the elevated run. */
    av[0] = "xhcitr";
    av[1] = "/eject";
    av[2] = "id=PCI\\VEN_1B21&DEV_2421\\X";
    av[3] = "/result";
    av[4] = "Local\\Xhci98TrAnswer-12-34";
    av[5] = "/confirmed";
    av[6] = "0123456789ABCDEF";
    CHECK(tr_parse_args(7, av, &o, err, sizeof(err)) == 0 && o.Eject &&
              o.Result && o.Confirmed &&
              strcmp(o.ResultName, "Local\\Xhci98TrAnswer-12-34") == 0 &&
              strcmp(o.ConfirmedDigest, "0123456789ABCDEF") == 0,
          "/eject id= /result <name> /confirmed <digest>");
    CHECK(tr_parse_args(4, av, &o, err, sizeof(err)) == 1, "/result needs its name");
    av[4] = "C:\\T\\xtr1.tmp";
    CHECK(tr_parse_args(5, av, &o, err, sizeof(err)) == 1, "/result refuses a file");
    av[4] = "Local\\Xhci98TrAnswer-12-34";
    av[6] = "nothex";
    CHECK(tr_parse_args(7, av, &o, err, sizeof(err)) == 1, "/confirmed refuses a non-digest");
    av[1] = "/result";
    av[2] = "Local\\Xhci98TrAnswer-1";
    CHECK(tr_parse_args(3, av, &o, err, sizeof(err)) == 1,
          "/result without /eject refused");
    av[1] = "/confirmed";
    av[2] = "0123456789ABCDEF";
    CHECK(tr_parse_args(3, av, &o, err, sizeof(err)) == 1,
          "/confirmed without /eject refused");

    /* The answer. */
    n = tr_res_answer(4, "Windows refused: a program is using the device.",
                      buf, sizeof(buf));
    CHECK(n == strlen(buf), "the answer's length");
    CHECK(tr_res_read_answer(buf, &code, msg, sizeof(msg)) && code == 4 &&
              strcmp(msg, "Windows refused: a program is using the device.") == 0,
          "the answer read back");
    CHECK_EQ(tr_res_answer(0, "x", buf, 10), 0, "an answer that does not fit");
    CHECK(!tr_res_read_answer("", &code, msg, sizeof(msg)), "an empty section");
    CHECK(!tr_res_read_answer("XHCITR ANSWER 1\r\ncode x\r\n", &code, msg,
                              sizeof(msg)), "a bad code");
    CHECK(!tr_res_read_answer("XHCITR ANSWER 1\r\ncode 99999999999\r\n", &code,
                              msg, sizeof(msg)), "a code out of range");
    CHECK(!tr_res_read_answer("XHCITR ANSWER 1\r\n", &code, msg, sizeof(msg)),
          "no code");
    CHECK(tr_res_read_answer("XHCITR ANSWER 1\ncode 2\nabcdefghij", &code, msg, 5) &&
              strcmp(msg, "abcd") == 0, "the text is cut to its buffer");

    /* The window's result. */
    code = tr_elev_result(0, TR_ERROR_CANCELLED, 0, 0, 0, 0, "", msg, sizeof(msg));
    CHECK_EQ(code, INST_EXIT_REFUSED, "cancelled: nothing stopped, exit 1");
    CHECK(strstr(msg, "Nothing was stopped") != NULL &&
              strstr(msg, "Try again") != NULL && strstr(msg, "plug it in again") != NULL,
          "cancelled: try again, or re-plug");
    code = tr_elev_result(0, 1155UL, 0, 0, 0, 0, "", msg, sizeof(msg));
    CHECK_EQ(code, INST_EXIT_REFUSED, "no runas: exit 1");
    CHECK(strstr(msg, "Run this as an") != NULL, "no runas: the plain message");
    code = tr_elev_result(1, 0, 0, 0, 0, 0, "", msg, sizeof(msg));
    CHECK_EQ(code, INST_EXIT_FAILED, "no end within the wait: exit 2");
    CHECK(strstr(msg, "before you unplug it") != NULL, "no end: look first");
    code = tr_elev_result(1, 0, 1, 0, 1, 0, "Windows has stopped X.", msg,
                          sizeof(msg));
    CHECK(code == INST_EXIT_DONE && strcmp(msg, "Windows has stopped X.") == 0,
          "stopped: the run's own text");
    code = tr_elev_result(1, 0, 1, 4, 1, 4, "Windows refused: a program is using "
                          "the device.", msg, sizeof(msg));
    CHECK(code == INST_EXIT_STEPS && strstr(msg, "program") != NULL,
          "vetoed: the run's own reason, exit 4");
    code = tr_elev_result(1, 0, 1, 4, 1, 0, "Windows has stopped X.", msg,
                          sizeof(msg));
    CHECK(code == INST_EXIT_STEPS && strstr(msg, "stopped X") == NULL,
          "an answer whose code is not the exit code is not trusted");
    code = tr_elev_result(1, 0, 1, 1, 0, 0, "", msg, sizeof(msg));
    CHECK_EQ(code, INST_EXIT_REFUSED, "refused with no answer: exit 1");
    code = tr_elev_result(1, 0, 1, 0, 0, 0, "", msg, sizeof(msg));
    CHECK_EQ(code, INST_EXIT_FAILED,
             "exit 0 with no answer: not known, never 'stopped'");
    code = tr_elev_result(1, 0, 1, 0xC0000005UL, 0, 0, "", msg, sizeof(msg));
    CHECK(code == INST_EXIT_FAILED && strstr(msg, "before you unplug") != NULL,
          "a crash: look before unplugging");
}

/* ---- the drivers' logging levels (record 25 section 7.5) ----------------- */

static void test_levels(void)
{
    char err[256];
    char line[128];
    char msg[512];
    char *av[8];
    TR_OPTS o;
    TR_RVAL cur;
    TR_LEVEL l;
    TR_LEVEL ls[3];
    TR_TEXT t;
    int drv;
    int lv;
    int code;
    TR_U32 k;
    int maxLen = 0;

    /* The level text, one line per level. */
    CHECK(strcmp(tr_level_text(0), "off") == 0 &&
              strcmp(tr_level_text(1), "counters only") == 0 &&
              strcmp(tr_level_text(2), "the log (default)") == 0 &&
              strcmp(tr_level_text(3), "adds port status") == 0 &&
              strcmp(tr_level_text(4), "adds memory addresses (for developers)") == 0,
          "levels: the owner's words, 0 to 4");
    CHECK(tr_level_text(5) == NULL && tr_level_text(0xFFFFFFFFUL) == NULL,
          "levels: no text out of range");
    CHECK(strcmp(tr_level_driver(TR_LV_XHCI98), "xhci98") == 0 &&
              strcmp(tr_level_driver(TR_LV_XHCICM), "xhcicm") == 0 &&
              tr_level_driver(2) == NULL && tr_level_driver(-1) == NULL,
          "levels: the drivers' names");

    /* Reading: each driver's one value. */
    cur.State = TR_RV_ABSENT;
    cur.Value = 0;
    tr_level_pick(NULL, &cur, &l);
    CHECK(l.State == TR_RV_ABSENT && l.Level == TR_LV_DEFAULT,
          "an absent value reads as the default 2");
    tr_level_line(TR_LV_XHCI98, &l, line, sizeof(line));
    CHECK(strcmp(line, "2 - the log (default) (no value set)") == 0,
          "xhci98: absent, the line");
    tr_level_line(TR_LV_XHCICM, &l, line, sizeof(line));
    CHECK(strcmp(line, "2 - the log (default) (no value set)") == 0,
          "xhcicm: absent, the line");
    cur.State = TR_RV_DWORD;
    cur.Value = 3;
    tr_level_pick(NULL, &cur, &l);
    CHECK(l.Level == 3, "3 read");
    tr_level_line(TR_LV_XHCI98, &l, line, sizeof(line));
    CHECK(strcmp(line, "3 - adds port status (XhciLogVerbosity)") == 0,
          "xhci98: 3, the line names its value");
    tr_level_line(TR_LV_XHCICM, &l, line, sizeof(line));
    CHECK(strcmp(line, "3 - adds port status (XhciCmLogVerbosity)") == 0,
          "xhcicm: 3, the line names its value");
    CHECK(strcmp(tr_level_value_name(TR_LV_XHCI98), "XhciLogVerbosity") == 0 &&
              strcmp(tr_level_value_name(TR_LV_XHCICM), "XhciCmLogVerbosity") == 0,
          "each driver's one value name");
    cur.Value = 0;
    tr_level_pick(NULL, &cur, &l);
    CHECK(l.Level == 0, "0 is a level, not absent");
    cur.Value = 4;
    tr_level_pick(NULL, &cur, &l);
    CHECK(l.Level == 4, "4 is a level");
    cur.Value = 5;
    tr_level_pick(NULL, &cur, &l);
    CHECK(l.Level == -1 && l.Value == 5, "5 is out of range");
    cur.Value = 7;
    tr_level_pick(NULL, &cur, &l);
    tr_level_line(TR_LV_XHCI98, &l, line, sizeof(line));
    CHECK(l.Level == -1 &&
              strcmp(line, "7 - out of range, not 0-4 (XhciLogVerbosity)") == 0,
          "xhci98: out of range shown as such");
    tr_level_line(TR_LV_XHCICM, &l, line, sizeof(line));
    CHECK(strcmp(line, "7 - out of range, not 0-4 (XhciCmLogVerbosity)") == 0,
          "xhcicm: out of range shown as such");
    cur.Value = 0xFFFFFFFFUL;
    tr_level_pick(NULL, &cur, &l);
    tr_level_line(TR_LV_XHCI98, &l, line, sizeof(line));
    CHECK(l.Level == -1 && strncmp(line, "4294967295 - out of range", 25) == 0,
          "the largest DWORD, unsigned");
    cur.State = TR_RV_OTHER;
    tr_level_pick(NULL, &cur, &l);
    tr_level_line(TR_LV_XHCICM, &l, line, sizeof(line));
    CHECK(l.Level == -1 && strcmp(line, "XhciCmLogVerbosity is not a REG_DWORD") == 0,
          "another type");
    cur.State = TR_RV_UNREAD;
    tr_level_pick(NULL, &cur, &l);
    tr_level_line(TR_LV_XHCI98, &l, line, sizeof(line));
    CHECK(l.Level == -1 && strcmp(line, "not read") == 0, "not read");

    /* Every line fits the dialog and 79 columns with its indent. */
    for (k = 0; k <= 12; k++) {
        int s;
        for (s = TR_RV_ABSENT; s <= TR_RV_UNREAD; s++) {
            int dd;
            for (dd = 0; dd < TR_LV_DRIVERS; dd++) {
                cur.State = s;
                cur.Value = k == 12 ? 0xFFFFFFFFUL : k;
                tr_level_pick(NULL, &cur, &l);
                tr_level_line(dd, &l, line, sizeof(line));
                if ((int)strlen(line) > maxLen) {
                    maxLen = (int)strlen(line);
                }
                tr_level_pick(&cur, &cur, &l);
                tr_level_line(dd, &l, line, sizeof(line));
                if ((int)strlen(line) > maxLen) {
                    maxLen = (int)strlen(line);
                }
            }
        }
    }
    CHECK(maxLen <= 72, "levels: every line at most 72 characters");

    /* The block. */
    tr_text_init(&t, big, sizeof(big));
    tr_level_head(&t, TR_LV_XHCI98, 0, NULL);
    tr_level_head(&t, TR_LV_XHCICM, 1, NULL);
    cur.State = TR_RV_DWORD;
    cur.Value = 3;
    tr_level_pick(NULL, &cur, &l);
    tr_level_device(&t, TR_LV_XHCICM, "Thunderbolt(TM) Controller - 15EB", &l);
    CHECK(strcmp(big,
                 "xhci98 (XhciLogVerbosity in each controller's driver key):\r\n"
                 "  Machine-wide (Services\\USB, wins when set): not set\r\n"
                 "  No USB controller is driven by xhci98.\r\n"
                 "xhcicm (XhciCmLogVerbosity in each host interface's driver key):\r\n"
                 "  Machine-wide (Services\\USB, wins when set): not set\r\n"
                 "  Thunderbolt(TM) Controller - 15EB\r\n"
                 "    3 - adds port status (XhciCmLogVerbosity)\r\n") == 0,
          "levels: the report's block");

    /* Task 42-A.17: the machine-wide value wins when it is there, in any
     * type; the device's own is shown as not in force. */
    {
        TR_RVAL m;
        /* A query's answer as the drivers read it: a zero-length REG_NONE
         * is absent, a REG_NONE with data and a REG_QWORD are another
         * type, a REG_DWORD of four bytes the DWORD. */
        tr_rv_from_query(TR_Q_OK, 0, 0, 0, &m);
        CHECK(m.State == TR_RV_ABSENT, "query: zero-length REG_NONE is absent");
        tr_rv_from_query(TR_Q_OK, 0, 4, 0, &m);
        CHECK(m.State == TR_RV_OTHER, "query: REG_NONE with data is another type");
        tr_rv_from_query(TR_Q_OK, 11, 4, 0, &m);
        CHECK(m.State == TR_RV_OTHER, "query: a short REG_QWORD is another type");
        tr_rv_from_query(TR_Q_OK, 4, 4, 3, &m);
        CHECK(m.State == TR_RV_DWORD && m.Value == 3, "query: a DWORD");
        tr_rv_from_query(TR_Q_OK, 4, 2, 3, &m);
        CHECK(m.State == TR_RV_OTHER, "query: a 2-byte REG_DWORD is another type");
        tr_rv_from_query(TR_Q_NOT_FOUND, 0, 0, 0, &m);
        CHECK(m.State == TR_RV_ABSENT, "query: not found");
        tr_rv_from_query(TR_Q_MORE_DATA, 1, 40, 0, &m);
        CHECK(m.State == TR_RV_OTHER, "query: too long is another type");
        tr_rv_from_query(TR_Q_ERROR, 4, 4, 3, &m);
        CHECK(m.State == TR_RV_UNREAD, "query: another error is not read");
        /* A zero-length REG_NONE machine-wide leaves the device's level in
         * force, so a Set writes the device's key (lv_set: State absent). */
        cur.State = TR_RV_DWORD;
        cur.Value = 3;
        tr_rv_from_query(TR_Q_OK, 0, 0, 0, &m);
        tr_level_pick(&m, &cur, &l);
        CHECK(l.From == TR_LV_FROM_DEVICE && l.Level == 3,
              "query: a zero-length REG_NONE machine-wide does not override");
        m.State = TR_RV_ABSENT;
        m.Value = 0;
        cur.State = TR_RV_ABSENT;
        tr_level_pick(&m, &cur, &l);
        CHECK(l.From == TR_LV_FROM_NONE && l.Level == TR_LV_DEFAULT,
              "machine-wide: absent from both is the default, from neither");
        cur.State = TR_RV_DWORD;
        cur.Value = 1;
        tr_level_pick(&m, &cur, &l);
        CHECK(l.From == TR_LV_FROM_DEVICE && l.Level == 1,
              "machine-wide: per-device only");
        m.State = TR_RV_DWORD;
        m.Value = 4;
        cur.State = TR_RV_ABSENT;
        tr_level_pick(&m, &cur, &l);
        CHECK(l.From == TR_LV_FROM_MACHINE && l.Level == 4,
              "machine-wide only");
        tr_level_line(TR_LV_XHCICM, &l, line, sizeof(line));
        CHECK(strcmp(line, "4 - adds memory addresses (for developers) "
                           "(machine-wide)") == 0,
              "machine-wide: the line says where");
        cur.State = TR_RV_DWORD;
        cur.Value = 1;
        m.Value = 3;
        tr_level_pick(&m, &cur, &l);
        CHECK(l.From == TR_LV_FROM_MACHINE && l.Level == 3 &&
                  l.Device.State == TR_RV_DWORD && l.Device.Value == 1,
              "machine-wide: both, the machine-wide value wins");
        m.State = TR_RV_OTHER;
        tr_level_pick(&m, &cur, &l);
        CHECK(l.From == TR_LV_FROM_MACHINE && l.Level == -1,
              "machine-wide of another type still wins");
        tr_level_line(TR_LV_XHCI98, &l, line, sizeof(line));
        CHECK(strcmp(line, "XhciLogVerbosity machine-wide is not a REG_DWORD") == 0,
              "machine-wide: another type, the line");
        m.State = TR_RV_UNREAD;
        tr_level_pick(&m, &cur, &l);
        CHECK(l.From == TR_LV_FROM_MACHINE && l.Level == -1,
              "machine-wide not read: no level claimed");

        m.State = TR_RV_DWORD;
        m.Value = 3;
        tr_text_init(&t, big, sizeof(big));
        tr_level_head(&t, TR_LV_XHCI98, 1, &m);
        tr_level_pick(&m, &cur, &l);
        tr_level_device(&t, TR_LV_XHCI98,
                        "xHCI98 USB 3.x eXtensible Host Controller", &l);
        CHECK(strcmp(big,
                     "xhci98 (XhciLogVerbosity in each controller's driver key):\r\n"
                     "  Machine-wide (Services\\USB, wins when set): 3 - adds port status\r\n"
                     "  xHCI98 USB 3.x eXtensible Host Controller\r\n"
                     "    3 - adds port status (machine-wide)\r\n"
                     "    its own value, 1, is not in force\r\n") == 0,
              "levels: the block with both set");
        m.Value = 9;
        tr_text_init(&t, big, sizeof(big));
        tr_level_head(&t, TR_LV_XHCICM, 0, &m);
        CHECK(strstr(big, "wins when set): 9 - out of range\r\n") != NULL,
              "levels: a machine-wide value out of range");
    }

    /* One level for all, or not. */
    ls[0].Level = 3;
    ls[1].Level = 3;
    ls[2].Level = 2;
    lv = -9;
    CHECK(tr_level_common(ls, 2, &lv) && lv == 3, "common: two at 3");
    CHECK(!tr_level_common(ls, 3, &lv), "common: 3, 3 and 2 differ");
    CHECK(!tr_level_common(ls, 0, &lv), "common: none");
    ls[0].Level = -1;
    CHECK(!tr_level_common(ls, 1, &lv), "common: out of range is no level");

    /* The arguments, strictly. */
    CHECK(tr_level_arg("xhci98=3", &drv, &lv) && drv == TR_LV_XHCI98 && lv == 3,
          "arg: xhci98=3");
    CHECK(tr_level_arg("XHCICM=0", &drv, &lv) && drv == TR_LV_XHCICM && lv == 0,
          "arg: case aside");
    CHECK(tr_level_arg("xhcicm=4", &drv, &lv) && lv == 4, "arg: 4");
    CHECK(!tr_level_arg("xhci98=5", &drv, &lv), "arg: 5 refused");
    CHECK(!tr_level_arg("xhci98=-1", &drv, &lv), "arg: negative refused");
    CHECK(!tr_level_arg("xhci98=03", &drv, &lv), "arg: two digits refused");
    CHECK(!tr_level_arg("xhci98=", &drv, &lv), "arg: no digit refused");
    CHECK(!tr_level_arg("xhci98 3", &drv, &lv), "arg: no = refused");
    CHECK(!tr_level_arg("xhciuas=3", &drv, &lv), "arg: another driver refused");
    CHECK(!tr_level_arg("xhci99=3", &drv, &lv), "arg: a near name refused");
    CHECK(!tr_level_arg("", &drv, &lv), "arg: empty refused");
    CHECK(!tr_level_arg("xhci98=3 ", &drv, &lv), "arg: trailing space refused");
    CHECK(!tr_level_arg("xhci98=a", &drv, &lv), "arg: a letter refused");

    av[0] = "xhcitr";
    av[1] = "/setlevel";
    av[2] = "xhci98=3";
    av[3] = "xhcicm=1";
    CHECK(tr_parse_args(4, av, &o, err, sizeof(err)) == 0 && o.SetLevel &&
              o.Level[TR_LV_XHCI98] == 3 && o.Level[TR_LV_XHCICM] == 1 && !o.Result,
          "/setlevel xhci98=3 xhcicm=1");
    CHECK(tr_parse_args(3, av, &o, err, sizeof(err)) == 0 &&
              o.Level[TR_LV_XHCI98] == 3 && o.Level[TR_LV_XHCICM] == -1,
          "/setlevel xhci98 alone; xhcicm not asked");
    av[2] = "xhcicm=2";
    CHECK(tr_parse_args(3, av, &o, err, sizeof(err)) == 0 &&
              o.Level[TR_LV_XHCI98] == -1 && o.Level[TR_LV_XHCICM] == 2,
          "/setlevel xhcicm alone");
    av[1] = "/tray";
    CHECK(tr_parse_args(2, av, &o, err, sizeof(err)) == 0 &&
              !o.SetLevel && o.Level[0] == -1 && o.Level[1] == -1,
          "no /setlevel: no level asked");
    av[1] = "-SETLEVEL";
    av[2] = "xhci98=4";
    av[3] = "/result";
    av[4] = "Local\\Xhci98TrAnswer-12-34";
    CHECK(tr_parse_args(5, av, &o, err, sizeof(err)) == 0 && o.SetLevel &&
              o.Result && o.Level[TR_LV_XHCI98] == 4 &&
              strcmp(o.ResultName, "Local\\Xhci98TrAnswer-12-34") == 0,
          "/setlevel with /result, the elevated run's form");
    av[4] = "C:\\T\\x.tmp";
    CHECK(tr_parse_args(5, av, &o, err, sizeof(err)) == 1,
          "/setlevel: /result refuses a file");
    av[3] = "/confirmed";
    av[4] = "0123456789ABCDEF";
    CHECK(tr_parse_args(5, av, &o, err, sizeof(err)) == 1 && has(err, "/confirmed"),
          "/setlevel: /confirmed refused");
    av[1] = "/setlevel";
    CHECK(tr_parse_args(2, av, &o, err, sizeof(err)) == 1 && has(err, "/setlevel"),
          "/setlevel with no level refused");
    av[2] = "xhci98=3";
    av[3] = "xhci98=2";
    CHECK(tr_parse_args(4, av, &o, err, sizeof(err)) == 1, "a driver twice refused");
    av[3] = "xhci98=9";
    CHECK(tr_parse_args(4, av, &o, err, sizeof(err)) == 1, "a level 9 refused");
    av[3] = "xhcicm=3";
    av[4] = "extra";
    CHECK(tr_parse_args(5, av, &o, err, sizeof(err)) == 1, "a stray word refused");
    av[2] = "xhci98=7";
    CHECK(tr_parse_args(3, av, &o, err, sizeof(err)) == 1,
          "out of range never reaches a write");
    av[2] = "xhci98=3";
    av[3] = "/setlevel";
    av[4] = "xhcicm=3";
    CHECK(tr_parse_args(5, av, &o, err, sizeof(err)) == 1, "/setlevel twice refused");
    av[3] = "/eject";
    av[4] = "id=X";
    CHECK(tr_parse_args(5, av, &o, err, sizeof(err)) == 1 && has(err, "alone"),
          "/setlevel with /eject refused");
    av[3] = "/tray";
    CHECK(tr_parse_args(4, av, &o, err, sizeof(err)) == 1, "/setlevel with /tray refused");
    av[3] = "/report";
    av[4] = "r.txt";
    CHECK(tr_parse_args(5, av, &o, err, sizeof(err)) == 1, "/setlevel with /report refused");
    av[1] = "/result";
    av[2] = "Local\\Xhci98TrAnswer-1";
    CHECK(tr_parse_args(3, av, &o, err, sizeof(err)) == 1,
          "/result alone still refused");

    /* The driver key the level is written under. */
    CHECK(tr_driver_key_ok("{36FC9E60-C465-11CF-8056-444553540000}\\0003"),
          "key: an NT class GUID and instance");
    CHECK(tr_driver_key_ok("USB\\0001") && tr_driver_key_ok("System\\0012"),
          "key: a 98 SE class name and instance");
    CHECK(!tr_driver_key_ok(""), "key: empty");
    CHECK(!tr_driver_key_ok("USB"), "key: no instance");
    CHECK(!tr_driver_key_ok("USB\\"), "key: an empty instance");
    CHECK(!tr_driver_key_ok("\\0001"), "key: no class");
    CHECK(!tr_driver_key_ok("USB\\0001\\x"), "key: a deeper path");
    CHECK(!tr_driver_key_ok("USB\\..\\0001"), "key: dots");
    CHECK(!tr_driver_key_ok("..\\0001"), "key: a parent");
    CHECK(!tr_driver_key_ok("USB\\00a1"), "key: a letter in the instance");
    CHECK(!tr_driver_key_ok("USB\\123456789"), "key: nine digits");
    CHECK(!tr_driver_key_ok("U SB\\0001"), "key: a space");

    /* The window's result of the elevated run. */
    code = tr_level_elev_result(0, TR_ERROR_CANCELLED, 0, 0, 0, 0, "", msg,
                                sizeof(msg));
    CHECK(code == INST_EXIT_REFUSED && has(msg, "Nothing was changed") &&
              has(msg, "administrator rights were not given"),
          "level run: cancelled");
    code = tr_level_elev_result(0, 1155UL, 0, 0, 0, 0, "", msg, sizeof(msg));
    CHECK(code == INST_EXIT_REFUSED && has(msg, "error 1155"),
          "level run: not started");
    code = tr_level_elev_result(1, 0, 0, 0, 0, 0, "", msg, sizeof(msg));
    CHECK(code == INST_EXIT_FAILED && has(msg, "has not finished"),
          "level run: no end within the wait");
    code = tr_level_elev_result(1, 0, 1, 0, 1, 0,
                                "xhci98: XhciLogVerbosity set to 3", msg,
                                sizeof(msg));
    CHECK(code == INST_EXIT_DONE && strcmp(msg, "xhci98: XhciLogVerbosity set to 3") == 0,
          "level run: its own answer");
    code = tr_level_elev_result(1, 0, 1, 2, 1, 0, "set", msg, sizeof(msg));
    CHECK(code == INST_EXIT_FAILED && strcmp(msg, "set") != 0,
          "level run: an answer whose code is not the exit code is not trusted");
    code = tr_level_elev_result(1, 0, 1, 0, 0, 0, "", msg, sizeof(msg));
    CHECK(code == INST_EXIT_FAILED && has(msg, "no answer"),
          "level run: exit 0 with no answer is not success");
    CHECK(has(tr_level_restart_question(), "only when they start") &&
              has(tr_level_restart_question(), "Restart Windows now?"),
          "level run: the restart offer");
}

static void test_login(void)
{
    CHECK(tr_tray_login_start(1, 1, 0) == 0, "login: /tray with 0 stops silently");
    CHECK(tr_tray_login_start(1, 1, 1) == 1, "login: /tray with 1 starts");
    CHECK(tr_tray_login_start(1, 1, 7) == 1, "login: any other number starts");
    CHECK(tr_tray_login_start(1, 0, 0) == 1, "login: no value (or not a DWORD) starts");
    CHECK(tr_tray_login_start(0, 1, 0) == 1, "login: a plain start always opens");
}

/* The "can run faster" notice (record 25 section 7.7, revision 18). */
static TR_FAST_IN fast_in(TR_U32 flags, unsigned int ex)
{
    TR_FAST_IN in;

    memset(&in, 0, sizeof(in));
    in.Ours = 1;
    in.Connected = 1;
    in.HaveV2 = 1;
    in.V2Flags = flags;
    in.ExSpeed = ex;
    in.Root = 1;
    return in;
}

static int fast_lines_fit(const char *s)
{
    int col = 0;

    for (; *s != '\0'; s++) {
        if (*s == '\r' || *s == '\n') {
            col = 0;
        } else if (++col > 79) {
            return 0;
        }
    }
    return 1;
}

/* Section 7.7 item 1: the qualifier answers settle on a
 * device's answer or the third request without one, and a scan drops the
 * keys it did not see. */
static void test_qual_cache(void)
{
    static TR_QUAL_CACHE c;
    static const unsigned char hs[10] = {10, 6, 0, 2, 0, 0, 0, 64, 1, 0};
    static const unsigned char bad[10] = {9, 6, 0, 2, 0, 0, 0, 64, 1, 0};
    char key[TR_FAST_KEY];
    int i;

    memset(&c, 0, sizeof(c));
    CHECK(tr_qual_get(&c, "a") == -1, "qual: unknown key asks");
    CHECK(tr_qual_put(&c, "a", NULL, 0) == 0, "qual: no answer counts 0 now");
    CHECK(tr_qual_get(&c, "a") == -1, "qual: one failure asks again");
    CHECK(tr_qual_put(&c, "a", hs, 10) == 1, "qual: then the answer");
    CHECK(tr_qual_get(&c, "a") == 1, "qual: settled High Speed capable");
    CHECK_EQ(c.N, 1, "qual: one entry");

    tr_qual_put(&c, "b", NULL, 0);
    tr_qual_put(&c, "b", NULL, 0);
    CHECK(tr_qual_get(&c, "b") == -1, "qual: two failures still ask");
    tr_qual_put(&c, "b", NULL, 0);
    CHECK(tr_qual_get(&c, "b") == 0, "qual: the third settles Full Speed");

    CHECK(tr_qual_put(&c, "c", bad, 10) == 0, "qual: malformed is 0");
    CHECK(tr_qual_get(&c, "c") == 0, "qual: and settled at once");
    CHECK(tr_qual_put(&c, "d", hs, 5) == 0, "qual: short is 0");
    CHECK(tr_qual_get(&c, "d") == 0, "qual: settled at once");

    /* A scan that sees a and c only: d, settled by its answer, is dropped
     * and asked again when it is back; b, settled by unanswered requests,
     * is kept. */
    tr_qual_begin(&c);
    CHECK(tr_qual_get(&c, "a") == 1, "qual: a seen");
    CHECK(tr_qual_get(&c, "c") == 0, "qual: c seen");
    tr_qual_end(&c);
    CHECK_EQ(c.N, 3, "qual: three kept");
    CHECK(tr_qual_get(&c, "d") == -1, "qual: d gone, asked again");
    CHECK(tr_qual_get(&c, "b") == 0, "qual: b kept, not asked");
    CHECK(tr_qual_get(&c, "a") == 1, "qual: a kept");
    CHECK(tr_qual_get(&c, "c") == 0, "qual: c kept");
    /* An entry asked in the scan (put) is seen too, and keeps its tries. */
    tr_qual_begin(&c);
    tr_qual_put(&c, "e", NULL, 0);
    tr_qual_end(&c);
    CHECK_EQ(c.N, 2, "qual: b and e kept");
    CHECK(tr_qual_get(&c, "e") == -1, "qual: e unsettled, kept");
    tr_qual_put(&c, "e", NULL, 0);
    tr_qual_put(&c, "e", NULL, 0);
    CHECK(tr_qual_get(&c, "e") == 0, "qual: e settled across scans");
    /* An unanswered request, then a scan without the device (gone, perhaps
     * with the recovery the request caused): settled, never asked again. */
    tr_qual_begin(&c);
    tr_qual_put(&c, "f", NULL, 0);
    tr_qual_end(&c);
    CHECK(tr_qual_get(&c, "f") == -1, "qual: f one try");
    tr_qual_begin(&c);
    tr_qual_end(&c);
    CHECK(tr_qual_get(&c, "f") == 0, "qual: f gone after a try: settled");
    tr_qual_begin(&c);
    CHECK(tr_qual_get(&c, "f") == 0, "qual: f back: not asked");
    tr_qual_end(&c);
    CHECK(tr_qual_get(&c, "f") == 0, "qual: f still settled");

    /* Turnover in a full cache, as the scan does it: every candidate
     * present marked first (new ones before retained ones), then the end,
     * then the requests. No retained key is lost. */
    memset(&c, 0, sizeof(c));
    tr_qual_begin(&c);
    for (i = 0; i < TR_QUAL_MAX; i++) {
        sprintf(key, "t%d", i);
        tr_qual_put(&c, key, (i % 2) ? hs : NULL, (i % 2) ? 10 : 0);
    }
    tr_qual_end(&c);
    CHECK_EQ(c.N, TR_QUAL_MAX, "qual: turnover: full");
    CHECK_EQ(tr_qual_room(&c, "x"), 0, "qual: full: a new key not asked");
    CHECK_EQ(tr_qual_room(&c, "t3"), 1, "qual: full: a kept key may be");
    tr_qual_put(&c, "x", hs, 10);
    CHECK_EQ(c.N, TR_QUAL_MAX, "qual: full: nothing replaced");
    CHECK(tr_qual_get(&c, "x") == -1, "qual: full: the new key not kept");
    CHECK(tr_qual_get(&c, "t0") == -1, "qual: full: the first kept");
    {
        int j;
        int ok = 1;
        tr_qual_begin(&c);
        for (j = 0; j < 400; j++) {
            sprintf(key, "n%d", j);
            if (tr_qual_get(&c, key) != -1) {
                ok = 0;
            }
        }
        for (j = 0; j < 100; j++) {
            sprintf(key, "t%d", j);
            (void)tr_qual_get(&c, key);
        }
        tr_qual_end(&c);
        for (j = 0; j < 400; j++) {
            sprintf(key, "n%d", j);
            if (!tr_qual_room(&c, key)) {
                ok = 0;
            }
            tr_qual_put(&c, key, hs, 10);
        }
        for (j = 0; j < 100; j++) {
            sprintf(key, "t%d", j);
            if (tr_qual_get(&c, key) == ((j % 2) ? 1 : -1)) {
                if (j % 2 == 0) {
                    tr_qual_put(&c, key, NULL, 0);
                }
            } else {
                ok = 0;
            }
        }
        for (j = 0; j < 100; j++) {
            sprintf(key, "t%d", j);
            if (tr_qual_get(&c, key) != ((j % 2) ? 1 : -1)) {
                ok = 0;
            }
        }
        for (j = 0; j < 400; j++) {
            sprintf(key, "n%d", j);
            if (tr_qual_get(&c, key) != 1) {
                ok = 0;
            }
        }
        CHECK(ok, "qual: turnover keeps the scan's keys, answers and tries");
    }

    /* More candidates than the balloon keeps (TR_FAST_MAX): repeated
     * scans keep every answer and every unsettled try. */
    memset(&c, 0, sizeof(c));
    for (i = 0; i < 3; i++) {
        int j;
        int ok = 1;
        tr_qual_begin(&c);
        for (j = 0; j < TR_FAST_MAX + 8; j++) {
            sprintf(key, "m%d", j);
            if (tr_qual_get(&c, key) < 0) {
                if (j % 2) {
                    tr_qual_put(&c, key, hs, 10);
                } else {
                    tr_qual_put(&c, key, NULL, 0);
                }
            }
        }
        tr_qual_end(&c);
        CHECK_EQ(c.N, TR_FAST_MAX + 8, "qual: every candidate kept");
        for (j = 0; j < TR_FAST_MAX + 8; j++) {
            sprintf(key, "m%d", j);
            if (tr_qual_get(&c, key) != (j % 2 ? 1 : (i < 2 ? -1 : 0))) {
                ok = 0;
            }
        }
        CHECK(ok, "qual: answers kept, tries counted across scans");
    }

    /* Full of keys whose requests went unanswered and whose devices went
     * away: every one kept, so none is asked again; a new key is not
     * asked at all. */
    memset(&c, 0, sizeof(c));
    tr_qual_begin(&c);
    for (i = 0; i < TR_QUAL_MAX; i++) {
        sprintf(key, "k%d", i);
        tr_qual_put(&c, key, NULL, 0);
    }
    tr_qual_end(&c);
    tr_qual_begin(&c);
    tr_qual_end(&c);
    CHECK_EQ(c.N, TR_QUAL_MAX, "qual: failure keys all kept");
    CHECK(tr_qual_get(&c, "k0") == 0 && tr_qual_get(&c, "k1023") == 0,
          "qual: failure keys settled, not asked again");
    CHECK_EQ(tr_qual_room(&c, "new"), 0, "qual: no room: the new key not asked");
    CHECK_EQ(tr_qual_room(NULL, "a"), 1, "qual: NULL: room");
    CHECK(tr_qual_get(NULL, "a") == -1, "qual: NULL asks");
    CHECK(tr_qual_put(NULL, "a", hs, 10) == 1, "qual: NULL still answers");
    tr_qual_begin(NULL);
    tr_qual_end(NULL);
}

static void test_fast(void)
{
    TR_FAST_IN in;
    TR_FAST f;
    char keys[3][TR_FAST_KEY];
    char key[TR_FAST_KEY];
    char out[400];
    char box[2048];
    TR_TEXT t;

    /* Declared and running speeds. */
    CHECK_EQ(tr_fast_can(1, TR_V2_F_SS_CAPABLE), TR_SPEED_SUPER, "can: SS");
    CHECK_EQ(tr_fast_can(1, TR_V2_F_SS_CAPABLE | TR_V2_F_SSP_CAPABLE),
             TR_SPEED_SUPERPLUS, "can: SSP");
    CHECK_EQ(tr_fast_can(1, TR_V2_F_SSP_CAPABLE), TR_SPEED_SUPERPLUS,
             "can: SSP alone");
    CHECK_EQ(tr_fast_can(1, TR_V2_F_SS_NOW), -1, "can: operating is not declaring");
    CHECK_EQ(tr_fast_can(0, TR_V2_F_SS_CAPABLE), -1, "can: no _EX_V2");

    /* A USB 3 stick on a USB 2.0 root port. */
    in = fast_in(TR_V2_F_SS_CAPABLE, TR_SPEED_HIGH);
    CHECK_EQ(tr_fast_check(&in, &f), 1, "SS at HS on a root port counts");
    CHECK_EQ(f.Now, TR_SPEED_HIGH, "runs at High Speed");
    CHECK_EQ(f.Can, TR_SPEED_SUPER, "can SuperSpeed");
    CHECK_EQ(f.Where, TR_FW_PORT2, "on a USB 2.0 port");
    in = fast_in(TR_V2_F_SS_CAPABLE, TR_SPEED_FULL);
    CHECK(tr_fast_check(&in, &f) && f.Now == TR_SPEED_FULL, "SS at FS counts");
    in = fast_in(TR_V2_F_SS_CAPABLE | TR_V2_F_SSP_CAPABLE, TR_SPEED_HIGH);
    CHECK(tr_fast_check(&in, &f) && f.Can == TR_SPEED_SUPERPLUS &&
              f.Where == TR_FW_PORT2, "SSP at HS: can SuperSpeedPlus");

    /* At its speed, or declaring nothing: never. */
    in = fast_in(TR_V2_F_SS_CAPABLE | TR_V2_F_SS_NOW, 2);
    CHECK_EQ(tr_fast_check(&in, &f), 0, "SS at SS: no");
    CHECK_EQ(f.Now, -1, "nothing filled");
    in = fast_in(TR_V2_F_SS_CAPABLE | TR_V2_F_SS_NOW | TR_V2_F_SSP_CAPABLE |
                 TR_V2_F_SSP_NOW, 2);
    CHECK_EQ(tr_fast_check(&in, &f), 0, "SSP at SSP: no");
    in = fast_in(0, TR_SPEED_HIGH);
    CHECK_EQ(tr_fast_check(&in, &f), 0, "a High Speed-only device: never");
    in = fast_in(0, TR_SPEED_LOW);
    CHECK_EQ(tr_fast_check(&in, &f), 0, "a Low Speed device: never");

    /* A SuperSpeedPlus device at 5 Gbit/s. */
    in = fast_in(TR_V2_F_SS_CAPABLE | TR_V2_F_SS_NOW | TR_V2_F_SSP_CAPABLE, 2);
    CHECK_EQ(tr_fast_check(&in, &f), 1, "SSP at 5 Gbit/s counts");
    CHECK_EQ(f.Now, TR_SPEED_SUPER, "runs at SuperSpeed");
    CHECK_EQ(f.Where, TR_FW_LINK, "a 5 Gbit/s link");
    /* 98 SE and ME: the _EX byte 3 or 4 with the operating flags. */
    in = fast_in(TR_V2_F_SS_CAPABLE | TR_V2_F_SS_NOW | TR_V2_F_SSP_CAPABLE, 3);
    CHECK(tr_fast_check(&in, &f) && f.Where == TR_FW_LINK, "98: byte 3, SSP capable");
    in = fast_in(TR_V2_F_SS_CAPABLE | TR_V2_F_SS_NOW | TR_V2_F_SSP_CAPABLE |
                 TR_V2_F_SSP_NOW, 4);
    CHECK_EQ(tr_fast_check(&in, &f), 0, "98: byte 4 at SSP: no");
    /* A byte the decision does not trust, with no operating flag. */
    in = fast_in(TR_V2_F_SS_CAPABLE, 3);
    CHECK_EQ(tr_fast_check(&in, &f), 0, "byte 3, no operating flag: no");
    in = fast_in(TR_V2_F_SS_CAPABLE, 0xFF);
    CHECK_EQ(tr_fast_check(&in, &f), 0, "an unknown byte: no");

    /* What never counts. */
    in = fast_in(TR_V2_F_SS_CAPABLE, TR_SPEED_HIGH);
    in.IsHub = 1;
    CHECK_EQ(tr_fast_check(&in, &f), 0, "a hub never counts itself");
    in = fast_in(TR_V2_F_SS_CAPABLE, TR_SPEED_HIGH);
    in.Ours = 0;
    CHECK_EQ(tr_fast_check(&in, &f), 0, "another driver's controller: no");
    in = fast_in(TR_V2_F_SS_CAPABLE, TR_SPEED_HIGH);
    in.Connected = 0;
    CHECK_EQ(tr_fast_check(&in, &f), 0, "not connected: no");
    in = fast_in(TR_V2_F_SS_CAPABLE, TR_SPEED_HIGH);
    in.HaveV2 = 0;
    CHECK_EQ(tr_fast_check(&in, &f), 0, "no _EX_V2: no");
    CHECK_EQ(tr_fast_check(NULL, &f), 0, "no port: no");

    /* Behind hubs. */
    in = fast_in(TR_V2_F_SS_CAPABLE, TR_SPEED_HIGH);
    in.Root = 0;
    in.HubKnown = 1;
    in.HubHaveV2 = 1;
    in.HubFlags = 0;
    in.HubExSpeed = TR_SPEED_HIGH;
    CHECK(tr_fast_check(&in, &f) && f.Where == TR_FW_HUB2 &&
              f.HubNow == TR_SPEED_HIGH, "behind a USB 2.0 hub");
    in.HubExSpeed = TR_SPEED_FULL;
    CHECK(tr_fast_check(&in, &f) && f.Where == TR_FW_HUB1 &&
              f.HubNow == TR_SPEED_FULL, "behind a Full Speed hub");
    in.HubExSpeed = TR_SPEED_HIGH;
    in.HubFlags = TR_V2_F_SS_CAPABLE;
    CHECK(tr_fast_check(&in, &f) && f.Where == TR_FW_HUB3,
          "behind a USB 3 hub at High Speed");
    in.HubExSpeed = TR_SPEED_FULL;
    CHECK(tr_fast_check(&in, &f) && f.Where == TR_FW_HUB3,
          "behind a USB 3 hub at Full Speed");
    in.HubKnown = 0;
    CHECK(tr_fast_check(&in, &f) && f.Where == TR_FW_HUB2 && f.HubNow == -1,
          "a hub not read: by default a USB 2.0 hub");
    in.HubKnown = 1;
    in.HubHaveV2 = 0;
    in.HubExSpeed = TR_SPEED_FULL;
    CHECK(tr_fast_check(&in, &f) && f.Where == TR_FW_HUB1,
          "a hub without _EX_V2: by its speed");

    /* The High Speed case (the owner, 2026-10-22): at Full Speed behind a
     * Full Speed hub, asked for its device_qualifier. */
    in = fast_in(0, TR_SPEED_FULL);
    in.Root = 0;
    in.HubKnown = 1;
    in.HubHaveV2 = 1;
    in.HubExSpeed = TR_SPEED_FULL;
    in.BcdUsb = 0x0200;
    in.HsQual = -1;
    CHECK_EQ(tr_fast_wants_qual(&in), 1, "HS case: a candidate is asked");
    CHECK_EQ(tr_fast_check(&in, &f), 0, "HS case: not asked yet: no");
    in.HsQual = 0;
    CHECK_EQ(tr_fast_check(&in, &f), 0, "HS case: refused: Full Speed only");
    in.HsQual = 1;
    CHECK_EQ(tr_fast_check(&in, &f), 1, "HS case: answered: counts");
    CHECK(f.Now == TR_SPEED_FULL && f.Can == TR_SPEED_HIGH &&
              f.Where == TR_FW_HUBFS && f.HubNow == TR_SPEED_FULL,
          "HS case: Full Speed, can High, behind a Full Speed hub");
    in.BcdUsb = 0x0110;
    CHECK_EQ(tr_fast_wants_qual(&in), 0, "HS case: USB 1.1 is not asked");
    CHECK_EQ(tr_fast_check(&in, &f), 0, "HS case: USB 1.1 never counts");
    in.BcdUsb = 0x0200;
    in.HubExSpeed = TR_SPEED_HIGH;
    CHECK_EQ(tr_fast_wants_qual(&in), 0, "HS case: behind a High Speed hub: no");
    CHECK_EQ(tr_fast_check(&in, &f), 0, "HS case: a Full Speed device there: no");
    in.HubExSpeed = TR_SPEED_FULL;
    in.Root = 1;
    CHECK_EQ(tr_fast_wants_qual(&in), 0, "HS case: a root port is never asked");
    in.Root = 0;
    in.HubKnown = 0;
    CHECK_EQ(tr_fast_wants_qual(&in), 0, "HS case: a hub not read: not asked");
    in.HubKnown = 1;
    in.UnderTt = 1;
    CHECK_EQ(tr_fast_wants_qual(&in), 0,
             "HS case: the Full Speed hub behind a High Speed one: not asked");
    in.HsQual = 1;
    CHECK_EQ(tr_fast_check(&in, &f), 0, "HS case: behind a TT never counts");
    in.HsQual = -1;
    in.UnderTt = 0;
    in.ExSpeed = TR_SPEED_HIGH;
    CHECK_EQ(tr_fast_wants_qual(&in), 0, "HS case: at High Speed: no");
    in.ExSpeed = TR_SPEED_FULL;
    in.IsHub = 1;
    CHECK_EQ(tr_fast_wants_qual(&in), 0, "HS case: a hub: no");
    in.IsHub = 0;
    in.Ours = 0;
    CHECK_EQ(tr_fast_wants_qual(&in), 0, "HS case: another controller: no");
    in.Ours = 1;
    in.V2Flags = TR_V2_F_SS_CAPABLE;
    CHECK_EQ(tr_fast_wants_qual(&in), 0, "HS case: SuperSpeed-capable: the SS case");
    CHECK(tr_fast_check(&in, &f) && f.Can == TR_SPEED_SUPER && f.Where == TR_FW_HUB1,
          "SS-capable at Full Speed behind a Full Speed hub: the SS case");
    CHECK_EQ(tr_fast_wants_qual(NULL), 0, "HS case: no port");
    {
        static const unsigned char q[10] = { 10, 6, 0, 2, 0, 0, 0, 64, 1, 0 };
        static const unsigned char d[10] = { 18, 1, 0, 2, 0, 0, 0, 64, 1, 0 };
        CHECK_EQ(tr_qual_ok(q, 10), 1, "qualifier: 10 bytes, type 6");
        CHECK_EQ(tr_qual_ok(q, 9), 0, "qualifier: short");
        CHECK_EQ(tr_qual_ok(d, 10), 0, "qualifier: a device descriptor is not one");
        CHECK_EQ(tr_qual_ok(NULL, 10), 0, "qualifier: none");
    }
    in = fast_in(0, TR_SPEED_FULL);
    in.Root = 0;
    in.HubKnown = 1;
    in.HubHaveV2 = 1;
    in.HubExSpeed = TR_SPEED_FULL;
    in.BcdUsb = 0x0200;
    in.HsQual = 1;
    tr_fast_check(&in, &f);
    tr_fast_balloon(out, sizeof(out), "USB Flash Disk (G:)", &f, 1);
    CHECK(strcmp(out, "USB Flash Disk (G:) runs at Full Speed (12 Mbit/s) behind "
                      "a Full Speed hub; it can run at High Speed (480 Mbit/s). "
                      "Click here for what to try.") == 0, "HS case: the balloon");
    CHECK(strstr(tr_fast_try(TR_FW_HUBFS), "rather than through this Full Speed hub")
              != NULL, "HS case: try");

    /* The texts. */
    CHECK(strstr(tr_fast_try(TR_FW_PORT2), "fell back to USB 2.0") != NULL,
          "try: a USB 2.0 port");
    CHECK(strstr(tr_fast_try(TR_FW_HUB2), "USB 3 hub") != NULL, "try: a hub");
    CHECK(strcmp(tr_fast_try(TR_FW_HUB1), tr_fast_try(TR_FW_HUB2)) == 0,
          "try: a Full Speed hub as a USB 2.0 one");
    CHECK(strstr(tr_fast_try(TR_FW_HUB3), "Connect the hub") != NULL,
          "try: the hub to a USB 3 port");
    CHECK(strstr(tr_fast_try(TR_FW_LINK), "10 Gbit/s or more") != NULL,
          "try: a faster port");
    CHECK(tr_fast_try(TR_FW_NONE)[0] == '\0', "try: nothing for none");
    CHECK(strcmp(tr_fast_speed(TR_SPEED_HIGH), "High Speed (480 Mbit/s)") == 0,
          "speed: High");
    CHECK(strcmp(tr_fast_speed(TR_SPEED_SUPERPLUS),
                 "SuperSpeedPlus (10 Gbit/s or more)") == 0, "speed: SSP");
    CHECK(strcmp(tr_fast_speed(-1), "a speed not reported") == 0, "speed: none");

    /* Keys, and which are new. */
    tr_fast_key(key, sizeof(key), "{36fc9e60}\\0003", "3.2", 0x0781, 0x5581);
    CHECK(strcmp(key, "{36fc9e60}\\0003|3.2|0781:5581") == 0, "the key");
    tr_fast_key(keys[0], TR_FAST_KEY, "{36fc9e60}\\0003", "3.2", 0x0781, 0x5581);
    tr_fast_key(keys[1], TR_FAST_KEY, "{36fc9e60}\\0003", "4", 0x0781, 0x5581);
    tr_fast_key(keys[2], TR_FAST_KEY, NULL, NULL, 0x10000, 0x1);
    CHECK(strcmp(keys[2], "||0000:0001") == 0, "a key of nothing");
    CHECK_EQ(tr_fast_new((const char (*)[TR_FAST_KEY])keys, 2, key), 0,
             "the same device again: not new");
    tr_fast_key(key, sizeof(key), "{36fc9e60}\\0003", "3.1", 0x0781, 0x5581);
    CHECK_EQ(tr_fast_new((const char (*)[TR_FAST_KEY])keys, 2, key), 1,
             "moved to another port: new");
    CHECK_EQ(tr_fast_new((const char (*)[TR_FAST_KEY])keys, 1, keys[1]), 1,
             "past the count: new");
    CHECK_EQ(tr_fast_new(NULL, 0, key), 1, "no last set: new");

    /* The balloon. */
    in = fast_in(TR_V2_F_SS_CAPABLE, TR_SPEED_HIGH);
    tr_fast_check(&in, &f);
    tr_fast_balloon(out, sizeof(out), "USB Mass Storage Device (F:)", &f, 1);
    CHECK(strcmp(out, "USB Mass Storage Device (F:) runs at High Speed "
                      "(480 Mbit/s) on a USB 2.0 port; it can run at SuperSpeed "
                      "(5 Gbit/s). Click here for what to try.") == 0,
          "balloon: one");
    CHECK(strlen(out) < 256, "balloon: fits szInfo");
    tr_fast_balloon(out, sizeof(out), "Stick", &f, 3);
    CHECK(strcmp(out, "3 USB devices run slower than they can: Stick and 2 "
                      "more. Click here for what to try.") == 0, "balloon: three");
    CHECK(strlen(TR_FAST_TITLE) < 64, "the title fits szInfoTitle");

    /* The box's entry. */
    in = fast_in(TR_V2_F_SS_CAPABLE, TR_SPEED_HIGH);
    in.Root = 0;
    in.HubKnown = 1;
    in.HubHaveV2 = 1;
    in.HubExSpeed = TR_SPEED_HIGH;
    tr_fast_check(&in, &f);
    tr_text_init(&t, box, sizeof(box));
    tr_fast_entry(&t, "USB Mass Storage Device (F:)", 0x0781, 0x5581,
                  "Port 3.2 of Intel(R) USB 3.0 eXtensible Host Controller",
                  "Generic USB Hub", &f);
    CHECK(strstr(box, "USB Mass Storage Device (F:) (0781:5581)\r\n") == box,
          "entry: the name first");
    CHECK(strstr(box, "  Port 3.2 of Intel(R) USB 3.0 eXtensible Host Controller\r\n")
              != NULL, "entry: the port");
    CHECK(strstr(box, "  Behind Generic USB Hub (High Speed (480 Mbit/s))\r\n") != NULL,
          "entry: the hub");
    CHECK(strstr(box, "  Runs at:    High Speed (480 Mbit/s)\r\n") != NULL,
          "entry: runs at");
    CHECK(strstr(box, "  Can run at: SuperSpeed (5 Gbit/s)\r\n") != NULL,
          "entry: can run at");
    CHECK(strstr(box, "  Try: Connect it to a USB 3 port of this computer, or through a "
                      "USB 3 hub.\r\n") != NULL, "entry: try, on one line");
    CHECK(fast_lines_fit(box), "entry: 79 columns");
    CHECK(!t.Overflow, "entry: no overflow");
    /* No hub line on a root port; a long port line wrapped. */
    in = fast_in(TR_V2_F_SS_CAPABLE, TR_SPEED_HIGH);
    tr_fast_check(&in, &f);
    tr_text_init(&t, box, sizeof(box));
    tr_fast_entry(&t, "A device with a rather long name that goes on", 1, 2,
                  "Port 15 of AMD USB 3.10 eXtensible Host Controller - 1.10 "
                  "(Microsoft) - AMD 1022:15B6 (behind PCI bridge 00:08.1)", NULL, &f);
    CHECK(strstr(box, "Behind") == NULL, "entry: no hub line on a root port");
    CHECK(fast_lines_fit(box), "entry: a long port line in 79 columns");
    CHECK(strstr(box, "fell back to USB 2.0") != NULL, "entry: the root port's try");
    CHECK(strstr(box, "  Try: Connect it to a USB 3 port of this computer (often blue, or "
                      "marked SS).\r\n       If it is already in one,") != NULL,
          "entry: try wrapped and indented");

    /* The menu line and the tooltip. */
    tr_fast_menu(out, sizeof(out), 1);
    CHECK(strcmp(out, "1 USB device can run faster...") == 0, "menu: one");
    tr_fast_menu(out, sizeof(out), 2);
    CHECK(strcmp(out, "2 USB devices can run faster...") == 0, "menu: two");
    tr_fast_tip(out, sizeof(out), 12);
    CHECK(strcmp(out, "xHCI98 USB Tree: 12 USB devices can run faster") == 0 &&
              strlen(out) < 64, "tooltip: within 98 SE's 64");
    CHECK(strcmp(TR_FAST_VALUE, "FasterNotice") == 0, "the user's value");
}

static void test_labels(void)
{
    unsigned int ven;
    unsigned int dev;
    char out[160];

    CHECK(tr_pci_ids("PCI\\VEN_8086&DEV_02ED&SUBSYS_22B017AA&REV_00\\3&11583659&0&A0",
                     &ven, &dev) && ven == 0x8086U && dev == 0x02EDU, "pci ids: NT instance id");
    CHECK(tr_pci_ids("pci\\ven_1b36&dev_000d&subsys_11001af4\\BUS_00&DEV_04&FUNC_00",
                     &ven, &dev) && ven == 0x1B36U && dev == 0x000DU, "pci ids: 98, lower case");
    CHECK(!tr_pci_ids("USB\\VID_0781&PID_5581\\1", &ven, &dev) && ven == 0,
          "pci ids: not PCI");
    CHECK(!tr_pci_ids("PCI\\VEN_80", &ven, &dev), "pci ids: cut short");
    CHECK(!tr_pci_ids(NULL, &ven, &dev), "pci ids: none");
    CHECK(tr_pci_loc9x("PCI\\VEN_8086&DEV_8A13&SUBSYS_0\\BUS_00&DEV_0D&FUNC_02", &ven,
                       &dev) && ven == 0 && dev == 0x0D, "98 location: root bus");
    CHECK(!tr_pci_loc9x("PCI\\VEN_8086&DEV_15C1\\0000083000", &ven, &dev),
          "98 location: a path form is not one");

    tr_ctrl_suffix(out, sizeof(out), 1, 0x8086U, 0x02EDU, TR_TB_NOT);
    CHECK(strcmp(out, " - Intel 8086:02ED") == 0, "controller: Intel PCH xHCI");
    tr_ctrl_suffix(out, sizeof(out), 1, 0x8086U, 0x15C1U, TR_TB_TB3);
    CHECK(strcmp(out, " - Thunderbolt 3 controller's USB (8086:15C1)") == 0,
          "controller: Alpine Ridge's own xHCI");
    tr_ctrl_suffix(out, sizeof(out), 1, 0x8086U, 0x8A13U, TR_TB_USB4);
    CHECK(strcmp(out, " - USB4 controller's USB (8086:8A13)") == 0, "controller: USB4");
    tr_ctrl_suffix(out, sizeof(out), 0, 0, 0, TR_TB_UNKNOWN);
    CHECK(strcmp(out, " - Thunderbolt controller's USB") == 0,
          "controller: an NHI's, no ids, no generation");
    tr_ctrl_suffix(out, sizeof(out), 1, 0xABCDU, 0x0001U, TR_TB_NOT);
    CHECK(strcmp(out, " - vendor ABCD ABCD:0001") == 0, "controller: unknown vendor");
    tr_ctrl_suffix(out, sizeof(out), 0, 0, 0, TR_TB_NOT);
    CHECK(out[0] == '\0', "controller: nothing read, nothing added");

    tr_roothub_label(out, sizeof(out), 18, 1, 14, 4);
    CHECK(strcmp(out, "Root hub - 18 ports (14 USB 2.0, 4 USB 3.x)") == 0, "root hub: split");
    tr_roothub_label(out, sizeof(out), 1, 1, 1, 0);
    CHECK(strcmp(out, "Root hub - 1 port (1 USB 2.0, 0 USB 3.x)") == 0, "root hub: one port");
    tr_roothub_label(out, sizeof(out), 2, 0, 0, 0);
    CHECK(strcmp(out, "Root hub - 2 ports") == 0, "root hub: protocols unknown");
    tr_roothub_label(out, sizeof(out), 0, 1, 0, 0);
    CHECK(strcmp(out, "Root hub") == 0, "root hub: no ports read");
    tr_roothub_label(out, 12, 18, 1, 14, 4);
    CHECK(strlen(out) == 11, "root hub: a small buffer");

    CHECK(strcmp(tr_port_tag(1, 0x07UL), ", USB 3") == 0, "port tag: USB 3");
    CHECK(strcmp(tr_port_tag(1, 0x03UL), ", USB 2") == 0, "port tag: USB 2");
    CHECK(strcmp(tr_port_tag(1, 0x01UL), ", USB 1.1") == 0, "port tag: USB 1.1");
    CHECK(strcmp(tr_port_tag(1, 0UL), "") == 0, "port tag: none reported");
    CHECK(strcmp(tr_port_tag(0, 0x07UL), "") == 0, "port tag: no _EX_V2");
}

static void test_resources(void)
{
    unsigned char d[256];
    char buf[1024];
    TR_TEXT t;

    /* An allocated memory range (MEM_DES, 32 bytes). */
    memset(d, 0, sizeof(d));
    put32(d, 0, 0);
    put32(d, 4, 36);
    put32(d, 8, 0xE0000000UL);
    put32(d, 16, 0xE00FFFFFUL);
    tr_text_init(&t, buf, sizeof(buf));
    CHECK(tr_res_text(&t, TR_RES_MEM, d, 32, 1, 0) &&
          strcmp(buf, "    Memory        E0000000-E00FFFFF\r\n") == 0, "res: memory");
    /* Above 4 GB, no 64-bit arithmetic. */
    put32(d, 12, 0x4UL);
    put32(d, 20, 0x4UL);
    tr_text_init(&t, buf, sizeof(buf));
    CHECK(tr_res_text(&t, TR_RES_MEMLARGE, d, 32, 1, 0) &&
          has(buf, "4E0000000-4E00FFFFF"), "res: memory above 4 GB");
    /* I/O (IO_DES, 28 bytes). */
    memset(d, 0, sizeof(d));
    put32(d, 8, 0xE000UL);
    put32(d, 16, 0xE01FUL);
    tr_text_init(&t, buf, sizeof(buf));
    CHECK(tr_res_text(&t, TR_RES_IO, d, 28, 1, 0) &&
          strcmp(buf, "    I/O           E000-E01F\r\n") == 0, "res: I/O");
    /* IRQ (IRQ_DES_32 20 bytes, x64 24). */
    memset(d, 0, sizeof(d));
    put32(d, 8, 0x01UL);
    put32(d, 12, 10);
    tr_text_init(&t, buf, sizeof(buf));
    CHECK(tr_res_text(&t, TR_RES_IRQ, d, 20, 1, 0) &&
          strcmp(buf, "    IRQ           10 (level, shared)\r\n") == 0, "res: IRQ");
    tr_text_init(&t, buf, sizeof(buf));
    CHECK(!tr_res_text(&t, TR_RES_IRQ, d, 20, 1, 1) && has(buf, "could not be read"),
          "res: an x64 IRQ descriptor is 24 bytes");
    put32(d, 8, 0x02UL);
    put32(d, 12, 0xFFFFFFFEUL);
    tr_text_init(&t, buf, sizeof(buf));
    CHECK(tr_res_text(&t, TR_RES_IRQ, d, 24, 1, 1) &&
          has(buf, "message-signalled (-2)"), "res: an MSI on Vista and 7");
    /* DMA (16) and bus numbers (20). */
    memset(d, 0, sizeof(d));
    put32(d, 12, 3);
    tr_text_init(&t, buf, sizeof(buf));
    CHECK(tr_res_text(&t, TR_RES_DMA, d, 16, 1, 0) && has(buf, "DMA           3"),
          "res: DMA");
    memset(d, 0, sizeof(d));
    put32(d, 12, 7);
    put32(d, 16, 8);
    tr_text_init(&t, buf, sizeof(buf));
    CHECK(tr_res_text(&t, TR_RES_BUSNUMBER, d, 20, 1, 0) &&
          strcmp(buf, "    Bus numbers   07-08\r\n") == 0, "res: bus numbers");
    /* A requirement: one IRQ range (12 bytes after the header). */
    memset(d, 0, sizeof(d));
    put32(d, 0, 1);
    put32(d, 4, 12);
    put32(d, 20, 3);
    put32(d, 24, 15);
    put32(d, 28, 0x01UL);
    tr_text_init(&t, buf, sizeof(buf));
    CHECK(tr_res_text(&t, TR_RES_IRQ, d, 32, 0, 0) &&
          has(buf, "IRQ           one of 3-15 (level, shared)"), "res: an IRQ asked for");
    tr_text_init(&t, buf, sizeof(buf));
    CHECK(!tr_res_text(&t, TR_RES_IRQ, d, 31, 0, 0), "res: a range cut short");
    put32(d, 0, 100000UL);
    tr_text_init(&t, buf, sizeof(buf));
    CHECK(!tr_res_text(&t, TR_RES_IRQ, d, 32, 0, 0), "res: a count too large");
    /* A memory requirement: 4096 bytes anywhere below 4 GB. */
    memset(d, 0, sizeof(d));
    put32(d, 0, 1);
    put32(d, 32 + 8, 4096);
    put32(d, 32 + 20, 0xFFFFFFFFUL);
    tr_text_init(&t, buf, sizeof(buf));
    CHECK(tr_res_text(&t, TR_RES_MEM, d, 68, 0, 0) &&
          has(buf, "4096 bytes in 00000000-FFFFFFFF"), "res: memory asked for");
    /* Large memory asked for (MEM_LARGE_RANGE, 40 bytes, 8-byte length):
     * 8 GiB, then a second range. */
    memset(d, 0, sizeof(d));
    put32(d, 0, 2);
    put32(d, 32 + 12, 2);
    put32(d, 32 + 16, 0);
    put32(d, 32 + 20, 1);
    put32(d, 32 + 24, 0xFFFFFFFFUL);
    put32(d, 32 + 28, 0xFUL);
    put32(d, 72 + 8, 0x1000UL);
    put32(d, 72 + 24, 0xFFFFFFFFUL);
    tr_text_init(&t, buf, sizeof(buf));
    CHECK(tr_res_text(&t, TR_RES_MEMLARGE, d, 112, 0, 0) &&
          has(buf, "200000000h bytes in 100000000-FFFFFFFFF") &&
          has(buf, "00001000h bytes in 00000000-FFFFFFFF"),
          "res: large memory asked for, above 4 GiB and a second range");
    tr_text_init(&t, buf, sizeof(buf));
    CHECK(!tr_res_text(&t, TR_RES_MEMLARGE, d, 111, 0, 0), "res: large memory cut short");
    tr_text_init(&t, buf, sizeof(buf));
    CHECK(tr_res_text(&t, 0x8001UL, d, 36, 1, 0) && has(buf, "Private data  36 bytes"),
          "res: device-private data (an MSI's message on 7)");
    tr_text_init(&t, buf, sizeof(buf));
    CHECK(tr_res_text(&t, 0x8003UL, d, 5, 1, 0) && has(buf, "type 0x8003"),
          "res: another type, numbered");
    tr_text_init(&t, buf, sizeof(buf));
    CHECK(!tr_res_text(&t, TR_RES_MEM, d, 4, 1, 0), "res: header cut short");
    {
        const char *p = buf;
        int ok = 1;
        tr_text_init(&t, buf, sizeof(buf));
        memset(d, 0, sizeof(d));
        put32(d, 0, 1);
        put32(d, 32 + 8, 0xFFFFFFFFUL);
        put32(d, 32 + 12, 0xFFFFFFFFUL);
        put32(d, 32 + 16, 0xFFFFFFFFUL);
        put32(d, 32 + 20, 0xFFFFFFFFUL);
        put32(d, 32 + 24, 0xFFFFFFFFUL);
        tr_res_text(&t, TR_RES_MEM, d, 68, 0, 0);
        while (*p) {
            int k = 0;
            while (p[k] && p[k] != '\r') {
                k++;
            }
            if (k > 79) {
                ok = 0;
            }
            p += k;
            while (*p == '\r' || *p == '\n') {
                p++;
            }
        }
        CHECK(ok, "res: the widest line fits 79 columns");
    }
}

/* 42-A.5: the mark lines (record 21 section 7.4, revision 3, "The
 * viewer"). */
static MK_MARKS markSet;

static void test_marks(void)
{
    static const char lf1[] = "PartMgr\0XHCICM\0";
    static const char lf2[] = "xhcicmf\0xhcicmx\0";
    static const char lf3[] = "xhcicm";    /* no terminator at all */
    INST_U32 mem = tr_mark_member(6, 0x00030001UL);

    CHECK_EQ(mem, 0x0619UL, "member: bus 6, device 3, function 1");
    CHECK_EQ(tr_mark_member(0x106, 0x001F0007UL), 0x06FFUL,
             "member: masked to 8 bits of bus, 5 of device, 3 of function");
    CHECK(tr_msz_has(lf1, sizeof(lf1), "xhcicm"), "found without case");
    CHECK(!tr_msz_has(lf2, sizeof(lf2), "xhcicm"), "no prefix match");
    CHECK(tr_msz_has(lf3, 6, "xhcicm"), "an unterminated last string");
    CHECK(!tr_msz_has("", 1, "xhcicm"), "an empty value");
    CHECK(!tr_msz_has(lf1, 0, "xhcicm"), "no bytes");

    memset(&markSet, 0, sizeof(markSet));
    markSet.Count = 3;
    markSet.E[0].Member = mem;
    markSet.E[0].Bus = 7;
    markSet.E[0].Address = 0;
    markSet.E[0].Flags = MK_EF_REPORTED | MK_EF_LOCATED | MK_EF_ADD_ATT |
                         MK_EF_REMOVABLE;
    markSet.E[1].Member = mem;
    markSet.E[1].Bus = 8;
    markSet.E[1].Address = 0;
    markSet.E[1].Flags = MK_EF_REPORTED | MK_EF_LOCATED;
    markSet.E[1].Pending = 1234;
    markSet.E[2].Member = mem;
    markSet.E[2].Bus = 9;
    markSet.E[2].Flags = MK_EF_LOCATED;     /* a tombstone */
    CHECK_EQ(tr_mark_find(&markSet, mem, 7, 0, 0), 0, "the unit's entry");
    CHECK_EQ(tr_mark_find(&markSet, mem, 7, 0, 1), -1,
             "no departure pending on it");
    CHECK_EQ(tr_mark_find(&markSet, mem, 8, 0, 1), 1, "a pending departure");
    CHECK_EQ(tr_mark_find(&markSet, mem, 9, 0, 0), -1, "a tombstone is not one");
    CHECK_EQ(tr_mark_find(&markSet, mem + 8, 7, 0, 0), -1, "another member");
    CHECK_EQ(tr_mark_find(NULL, mem, 7, 0, 0), -1, "no answer");
    markSet.E[2].Bus = 7;
    markSet.E[2].Flags = MK_EF_REPORTED | MK_EF_LOCATED;
    CHECK_EQ(tr_mark_find(&markSet, mem, 7, 0, 0), -1, "two entries: none");
    markSet.E[2].Bus = 9;

    CHECK_EQ(tr_mark_state(0, NULL, -1, 0), TR_MK_OLD, "an older driver");
    CHECK_EQ(tr_mark_state(-1, NULL, -1, 1), TR_MK_FAILED, "a failed answer");
    CHECK_EQ(tr_mark_state(1, &markSet, 0, 1), TR_MK_LISTED, "Removable set");
    CHECK_EQ(tr_mark_state(1, &markSet, 0, 0), TR_MK_LISTED,
             "Removable set without the value (the first plug's attachment)");
    CHECK_EQ(tr_mark_state(1, &markSet, 1, 1), TR_MK_WRITTEN,
             "the value, no attachment: at the next start");
    CHECK_EQ(tr_mark_state(1, &markSet, -1, 1), TR_MK_WRITTEN,
             "the value, no entry");
    CHECK_EQ(tr_mark_state(1, &markSet, 1, 0), TR_MK_NOT, "no value, no attachment");
    markSet.E[1].Flags |= MK_EF_BUS_ATT;
    CHECK_EQ(tr_mark_state(1, &markSet, 1, 1), TR_MK_NOTLISTED,
             "the value and an attachment, Removable not set");
    CHECK_EQ(tr_mark_state(1, &markSet, 7, 1), TR_MK_WRITTEN,
             "an entry index past the count is none");
    CHECK(strcmp(tr_mark_text(TR_MK_LISTED), "listed by Windows") == 0,
          "listed text");
    CHECK(strcmp(tr_mark_text(TR_MK_WRITTEN), "mark written; takes effect at "
                 "the next start of the device") == 0, "written text");
    CHECK(strcmp(tr_mark_text(TR_MK_NOT), "not marked") == 0, "not text");
    CHECK(strcmp(tr_mksvc_text(TR_SV_RUNNING), "running") == 0, "running");
    CHECK(strcmp(tr_mksvc_text(TR_SV_ABSENT), "not installed") == 0,
          "not installed");
    CHECK(strcmp(tr_mksvc_text(99), "not read") == 0, "unknown state");
}

/* The tunnel watch after an NT eject: who confirms, how long, what is said. */
static void test_eject_watch(void)
{
    int s;

    /* An administrator's run confirms itself: the 10 s watch, every OS and
     * service state, as before. */
    for (s = TR_SV_UNREAD; s <= TR_SV_OTHER; s++) {
        CHECK_EQ(tr_eject_watch_kind(1, 0, 0, s), TR_EJW_PLAIN, "admin, XP+");
        CHECK_EQ(tr_eject_watch_kind(1, 0, 1, s), TR_EJW_PLAIN, "admin, 2000");
        CHECK_EQ(tr_eject_watch_kind(0, 1, 0, s), TR_EJW_PLAIN,
                 "the not-removable route");
    }
    /* No administrator, XP and later: the mark service confirms unless it
     * is known to be down. */
    CHECK_EQ(tr_eject_watch_kind(0, 0, 0, TR_SV_RUNNING), TR_EJW_SERVICE,
             "service running");
    CHECK_EQ(tr_eject_watch_kind(0, 0, 0, TR_SV_OTHER), TR_EJW_SERVICE,
             "service starting");
    CHECK_EQ(tr_eject_watch_kind(0, 0, 0, TR_SV_UNREAD), TR_EJW_SERVICE,
             "service state not read");
    CHECK_EQ(tr_eject_watch_kind(0, 0, 0, TR_SV_STOPPED), TR_EJW_UNCONFIRMED,
             "service stopped");
    CHECK_EQ(tr_eject_watch_kind(0, 0, 0, TR_SV_ABSENT), TR_EJW_UNCONFIRMED,
             "service not installed");
    /* 2000: problem 24 is no confirmation, so the service never writes one. */
    CHECK_EQ(tr_eject_watch_kind(0, 0, 1, TR_SV_RUNNING), TR_EJW_UNCONFIRMED,
             "2000, no administrator");

    /* The bound: a tool's own confirmation keeps 10 s; the service's adds
     * its idle period twice, that period being the service's own. */
    CHECK_EQ(mk_poll_ms(0), TR_EJW_MK_IDLE_MS, "the service's idle period");
    CHECK(mk_poll_ms(1) <= TR_EJW_MK_IDLE_MS, "its busy period is shorter");
    CHECK_EQ(tr_eject_watch_ms(TR_EJW_PLAIN), 10000UL, "plain bound");
    CHECK_EQ(tr_eject_watch_ms(TR_EJW_UNCONFIRMED), 10000UL, "unconfirmed bound");
    CHECK_EQ(tr_eject_watch_ms(TR_EJW_SERVICE),
             TR_EJW_OWN_MS + 2UL * mk_poll_ms(0), "service bound");
    CHECK_EQ(tr_eject_watch_ms(TR_EJW_SERVICE), 30000UL, "service bound 30 s");

    CHECK(strcmp(tr_eject_open_text(TR_EJW_PLAIN),
                 "The Thunderbolt driver has not confirmed that the PCIe "
                 "tunnel was closed.") == 0, "plain text unchanged");
    CHECK(strcmp(tr_eject_open_text(TR_EJW_SERVICE),
                 "The PCIe tunnel has not been seen closed yet. It closes "
                 "if the xHCI98 mark service confirms the eject to the "
                 "Thunderbolt driver, which the service does in the "
                 "background; an eject made as an administrator is confirmed "
                 "by this tool itself.") == 0, "service text");
    CHECK(strcmp(tr_eject_open_text(TR_EJW_UNCONFIRMED),
                 "The PCIe tunnel stays up until the device is unplugged, "
                 "because the eject was not confirmed to the Thunderbolt "
                 "driver; an eject made as an administrator is confirmed by "
                 "this tool itself.") == 0, "unconfirmed text");
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
    test_removals();
    test_keys_args_report();
    test_sizes();
    test_desc_lengths();
    test_usb_eject();
    test_elevated_stop();
    test_login();
    test_qual_cache();
    test_fast();
    test_levels();
    test_labels();
    test_resources();
    test_marks();
    test_eject_watch();
    {
        /* A UAS interface: bulk endpoints with companions and pipe usage. */
        static const unsigned char uas[] = {
            9, 2, 48, 0, 1, 1, 0, 0x80, 112,
            9, 4, 0, 1, 2, 0x08, 0x06, 0x62, 0,
            7, 5, 0x81, 0x02, 0x00, 0x04, 0,
            6, 0x30, 15, 0x04, 0, 0,
            4, 0x24, 3, 0,
            7, 5, 0x83, 0x01, 0x00, 0x04, 1,
            6, 0x30, 1, 0x02, 0, 0x04
        };
        char buf[4096];
        char out[48];
        TR_TEXT t;
        tr_text_init(&t, buf, sizeof(buf));
        tr_dec_config(&t, uas, sizeof(uas));
        CHECK(has(buf, "MaxStreams 4: 16 streams"), "companion: bulk MaxStreams");
        CHECK(has(buf, "Pipe usage descriptor (UAS)") && has(buf, "3 (data-in)"),
              "UAS pipe usage by name");
        CHECK(has(buf, "Mult 2: up to 3 bursts per service interval, each of up to 2 packets"),
              "companion: isochronous Mult");
        {
            /* configuration, interface (class cls, protocol pr), endpoint
             * (attributes ea, length el), companion (burst, bm) */
            unsigned char cf[37];
            static const unsigned int streams[4] = { 0, 16, 17, 31 };
            static const char *const want[4] = {
                "(no streams)", "MaxStreams 16: 65536 streams",
                "MaxStreams 17: reserved", "MaxStreams 31: reserved"
            };
            int k;
            for (k = 0; k < 8; k++) {
                static const unsigned char head[25] = {
                    9, 2, 37, 0, 1, 1, 0, 0x80, 50,
                    9, 4, 0, 0, 1, 0x08, 0x06, 0x62, 0,
                    7, 5, 0x81, 0x02, 0x00, 0x04, 0
                };
                memcpy(cf, head, sizeof(head));
                cf[25] = 6;
                cf[26] = 0x30;
                cf[27] = 1;
                cf[28] = 0;
                cf[29] = 0;
                cf[30] = 0;
                cf[31] = 4;
                cf[32] = 0x24;
                cf[33] = 1;
                cf[34] = 0;
                cf[35] = 0;
                cf[36] = 0;
                cf[2] = 35;
                if (k < 4) {
                    cf[28] = (unsigned char)streams[k];
                    tr_text_init(&t, buf, sizeof(buf));
                    tr_dec_config(&t, cf, 35);
                    CHECK(has(buf, want[k]), "companion: MaxStreams boundary");
                } else if (k == 4) {
                    cf[21] = 0x01;      /* isochronous */
                    cf[28] = 0x03;
                    tr_text_init(&t, buf, sizeof(buf));
                    tr_dec_config(&t, cf, 35);
                    CHECK(has(buf, "Mult 3: reserved"), "companion: Mult 3 reserved");
                } else if (k == 5) {
                    cf[21] = 0x01;
                    cf[28] = 0x82;
                    tr_text_init(&t, buf, sizeof(buf));
                    tr_dec_config(&t, cf, 35);
                    CHECK(has(buf, "Mult is ignored"), "companion: SSP isochronous, Mult ignored");
                } else if (k == 6) {
                    /* The endpoint cut to 6 bytes: the companion after it
                     * follows no valid endpoint. */
                    cf[18] = 6;
                    cf[24] = 6;
                    cf[25] = 0x30;
                    cf[26] = 1;
                    cf[27] = 0x04;
                    cf[28] = 0;
                    cf[29] = 0;
                    cf[30] = 4;
                    cf[31] = 0x24;
                    cf[32] = 1;
                    cf[33] = 0;
                    cf[2] = 34;
                    tr_text_init(&t, buf, sizeof(buf));
                    tr_dec_config(&t, cf, 34);
                    CHECK(has(buf, "(not after an endpoint)") && !has(buf, "streams"),
                          "companion: after a short endpoint");
                } else {
                    /* A second interface (not UAS) between the endpoint and
                     * the companion and pipe descriptor: both lose it. */
                    static const unsigned char two[] = {
                        9, 2, 44, 0, 2, 1, 0, 0x80, 50,
                        9, 4, 0, 0, 1, 0x08, 0x06, 0x62, 0,
                        7, 5, 0x81, 0x02, 0x00, 0x04, 0,
                        9, 4, 1, 0, 0, 0x03, 0x00, 0x00, 0,
                        6, 0x30, 1, 0x04, 0, 0,
                        4, 0x24, 1, 0
                    };
                    tr_text_init(&t, buf, sizeof(buf));
                    tr_dec_config(&t, two, sizeof(two));
                    CHECK(has(buf, "(not after an endpoint)") &&
                          !has(buf, "Pipe usage descriptor"),
                          "context: a new interface clears the endpoint and UAS");
                }
            }
        }
        tr_protocols_text(3UL, out, sizeof(out));
        CHECK(strcmp(out, "USB 1.1, USB 2.0") == 0, "protocols: two, no stray space");
        tr_protocols_text(4UL, out, sizeof(out));
        CHECK(strcmp(out, "USB 3.x") == 0, "protocols: one");
        tr_protocols_text(7UL, out, sizeof(out));
        CHECK(strcmp(out, "USB 1.1, USB 2.0, USB 3.x") == 0, "protocols: three");
        tr_protocols_text(0UL, out, sizeof(out));
        CHECK(strcmp(out, "none reported") == 0, "protocols: none");
    }
    {
        char buf[512];
        TR_TEXT t;
        const char *two[2];
        unsigned char e[CM_ST_ENTRY_BYTES];
        char name[TR_NAME_CHARS];
        two[0] = "Disk drive QEMU NVMe Ctrl, volume E:";
        two[1] = "Disk drive Samsung SSD, volumes F:, G:";
        tr_text_init(&t, buf, sizeof(buf));
        tr_confirm_lead(&t, "NVM Express Controller", "QEMU NVMe Ctrl (E:)", two, 2);
        CHECK(strcmp(buf, "This stops NVM Express Controller and everything on it:\r\n"
                          "    Disk drive QEMU NVMe Ctrl, volume E:\r\n"
                          "    Disk drive Samsung SSD, volumes F:, G:\r\n"
                          "\r\n"
                          "These devices will stop:\r\n") == 0,
              "confirm: two disks, exact CR LF, no full stop after a letter");
        CHECK(strstr(buf, "\r\r") == NULL, "confirm: no doubled CR");
        tr_text_init(&t, buf, sizeof(buf));
        tr_confirm_lead(&t, "USB xHCI Controller", "USB xHCI Controller", NULL, 0);
        CHECK(strcmp(buf, "Stop USB xHCI Controller?\r\n\r\nThese devices will stop:\r\n") == 0,
              "confirm: no disk");
        memset(e, 0, sizeof(e));
        memcpy(e + CM_ST_E_VENDOR, "Acme\tCorp", 9);
        memcpy(e + CM_ST_E_MODEL, " Dock\r\n  X1 ", 12);
        tr_router_name(e, name, sizeof(name));
        CHECK(strcmp(name, "Acme Corp Dock X1") == 0, "router name: tab, CR LF and runs made one space");
        memset(e, 0, sizeof(e));
        memcpy(e + CM_ST_E_VENDOR, "Ac\x01me", 5);
        memcpy(e + CM_ST_E_MODEL, "  ", 2);
        tr_router_name(e, name, sizeof(name));
        CHECK(strcmp(name, "Ac?me") == 0, "router name: a control byte is '?', a blank model drops");
        memset(e, 0, sizeof(e));
        memcpy(e + CM_ST_E_VENDOR, " \t ", 3);
        tr_router_name(e, name, sizeof(name));
        CHECK(strcmp(name, "(no name given)") == 0, "router name: blanks only");
    }
    {
        char s[64];
        strcpy(s, "QEMU NVM e Ctrl          ");
        tr_tidy_name(s);
        CHECK(strcmp(s, "QEMU NVM e Ctrl") == 0, "tidy: trailing spaces (98 SE model)");
        strcpy(s, "  QEMU   NVMe\tCtrl \r\n");
        tr_tidy_name(s);
        CHECK(strcmp(s, "QEMU NVMe Ctrl") == 0, "tidy: leading, inner runs, tab, line end");
        strcpy(s, "   ");
        tr_tidy_name(s);
        CHECK(s[0] == '\0', "tidy: blanks only");
        strcpy(s, "Disk");
        tr_tidy_name(s);
        CHECK(strcmp(s, "Disk") == 0, "tidy: nothing to do");
    }
    {
        char set[TR_LETTERS_BYTES];
        char out[128];
        set[0] = '\0';
        tr_letters_add(set, "f");
        tr_letters_add(set, "E;e:Z");
        CHECK(strcmp(set, "EFZ") == 0, "letters: merged, upper case, sorted, once");
        tr_letters_add(set, NULL);
        CHECK(strcmp(set, "EFZ") == 0, "letters: nothing added");
        tr_letters_text(set, out, sizeof(out));
        CHECK(strcmp(out, "E:, F:, Z:") == 0, "letters: text");
        tr_letters_text("", out, sizeof(out));
        CHECK(out[0] == '\0', "letters: none");
        tr_unit_label("NVM Express Controller", "Samsung SSD 970 EVO", "E", out,
                      sizeof(out));
        CHECK(strcmp(out, "Samsung SSD 970 EVO (E:)") == 0, "label: a disk and its letter");
        tr_unit_label("NVM Express Controller", "QEMU NVMe Ctrl", "EF", out,
                      sizeof(out));
        CHECK(strcmp(out, "QEMU NVMe Ctrl (E:, F:)") == 0, "label: two letters");
        tr_unit_label("NVM Express Controller", "QEMU NVMe Ctrl", "", out,
                      sizeof(out));
        CHECK(strcmp(out, "QEMU NVMe Ctrl") == 0, "label: a disk with no letter");
        tr_unit_label("USB xHCI Compliant Host Controller", "", "", out, sizeof(out));
        CHECK(strcmp(out, "USB xHCI Compliant Host Controller") == 0,
              "label: no disk keeps the device name");
        tr_disk_line("QEMU NVMe Ctrl", "E", out, sizeof(out));
        CHECK(strcmp(out, "Disk drive QEMU NVMe Ctrl, volume E:") == 0, "disk line: one");
        tr_disk_line("QEMU NVMe Ctrl", "EF", out, sizeof(out));
        CHECK(strcmp(out, "Disk drive QEMU NVMe Ctrl, volumes E:, F:") == 0,
              "disk line: two");
        tr_disk_line("QEMU NVMe Ctrl", "", out, sizeof(out));
        CHECK(strcmp(out, "Disk drive QEMU NVMe Ctrl") == 0, "disk line: none");
        {
            char all[TR_LETTERS_BYTES];
            char text[TR_LETTERS_TEXT_BYTES];
            char small[9];
            char line[200];
            char names[300];
            char lab[64];
            size_t k;
            all[0] = '\0';
            tr_letters_add(all, "ZYXWVUTSRQPONMLKJIHGFEDCBA");
            CHECK(strcmp(all, "ABCDEFGHIJKLMNOPQRSTUVWXYZ") == 0, "letters: the alphabet");
            tr_letters_text(all, text, sizeof(text));
            CHECK(strlen(text) == TR_LETTERS_TEXT_BYTES - 1 &&
                  strncmp(text, "A:, B:", 6) == 0 &&
                  strcmp(text + strlen(text) - 6, "Y:, Z:") == 0,
                  "letters: the whole alphabet fits TR_LETTERS_TEXT_BYTES");
            tr_letters_text(all, small, sizeof(small));
            CHECK(strcmp(small, "A:, B:") == 0, "letters: whole letters only, no separator left");
            tr_disk_line("D", all, line, sizeof(line));
            CHECK(strcmp(line + strlen(line) - 6, "Y:, Z:") == 0, "disk line: every letter");
            for (k = 0; k < sizeof(names) - 1; k++) {
                names[k] = (char)('a' + (k % 26));
            }
            names[sizeof(names) - 1] = '\0';
            tr_unit_label("Unit", names, "EF", lab, sizeof(lab));
            CHECK(strlen(lab) == sizeof(lab) - 1 &&
                  strcmp(lab + strlen(lab) - 12, "... (E:, F:)") == 0,
                  "label: long names end in ..., the letters kept whole");
            tr_unit_label("Unit", "Disk", all, lab, sizeof(lab));
            CHECK(strcmp(lab, "Disk") == 0, "label: no room for the letters keeps the name");
        }
    }
    {
        char s[128];
        CHECK(strcmp(tr_plural(1), "") == 0 && strcmp(tr_plural(0), "s") == 0 &&
              strcmp(tr_plural(2), "s") == 0, "plural");
        strcpy(s, "Windows has stopped the card\r\nreaders.\r\n\r\nYou can now\nunplug it.");
        tr_join_lines(s);
        CHECK(strcmp(s, "Windows has stopped the card readers.\r\n\r\nYou can now unplug it.") == 0,
              "join: wrapped lines joined, the paragraph kept");
        strcpy(s, "one line");
        tr_join_lines(s);
        CHECK(strcmp(s, "one line") == 0, "join: nothing to join");
        strcpy(s, "a\n   b\r\n");
        tr_join_lines(s);
        CHECK(strcmp(s, "a b ") == 0, "join: indentation after a break dropped");
        {
            /* Exact-sized buffers: the join never writes past the input. */
            char e1[6];
            char e2[3];
            char e3[5];
            char e4[8];
            char e5[6];
            memcpy(e1, "a\n\nbc", 6);
            tr_join_lines(e1);
            CHECK(strcmp(e1, "a\n\nbc") == 0, "join: LF-only paragraph kept, bc kept");
            memcpy(e2, "\n\n", 3);
            tr_join_lines(e2);
            CHECK(strcmp(e2, "\n\n") == 0, "join: a bare paragraph break");
            memcpy(e3, "a\n\nb", 5);
            tr_join_lines(e3);
            CHECK(strcmp(e3, "a\n\nb") == 0, "join: a\\n\\nb terminated");
            memcpy(e4, "a\r\n\nb\nc", 8);
            tr_join_lines(e4);
            CHECK(strcmp(e4, "a\n\nb c") == 0, "join: mixed endings");
            memcpy(e5, "ab\r\n\n", 6);
            tr_join_lines(e5);
            CHECK(strcmp(e5, "ab\n\n") == 0, "join: a trailing paragraph break");
        }
    }
    printf("test_tr: %d checks, %d failures\n", checks, failures);
    return failures;
}
