/*
 * test_tbs.c - host vectors for TBSPEED's pure half (tbspeed\tbs_core.c):
 * the command line and its refusals, the request and result blocks both
 * ends exchange (byte for byte, and what a reader refuses), and the
 * figures.
 */

#include <stdio.h>
#include <string.h>
#include "../tbspeed/tbs_core.h"
#include "test_harness.h"

static int parse(const char *line, TBS_OPTS *o, char *err)
{
    static char copy[512];
    char *argv[32];
    int argc = 0;
    char *p;
    strcpy(copy, line);
    argv[argc++] = "TBSPEED";
    for (p = strtok(copy, " "); p != NULL && argc < 32; p = strtok(NULL, " "))
        argv[argc++] = p;
    return tbs_parse(argc, argv, o, err, 96);
}

static void test_cmdline(void)
{
    TBS_OPTS o;
    char err[96];

    CHECK_EQ(parse("-s", &o, err), 0, "-s alone");
    CHECK_EQ(o.mode, TBS_MODE_SERVER, "-s: server");
    CHECK_EQ(o.port, TBS_PORT, "default port");
    CHECK_EQ(o.lenKb, TBS_DEF_LEN_KB, "default buffer");
    CHECK_EQ(o.interval, 1, "default interval");
    CHECK_EQ(o.once, 0, "not once");

    CHECK_EQ(parse("-c 169.254.10.20", &o, err), 0, "-c address");
    CHECK_EQ(o.mode, TBS_MODE_CLIENT, "-c: client");
    CHECK(strcmp(o.host, "169.254.10.20") == 0, "host kept");
    CHECK_EQ(o.seconds, TBS_DEF_SECONDS, "default seconds");
    CHECK_EQ(o.megabytes, 0, "timed by default");
    CHECK_EQ(o.reverse, 0, "not reversed");

    CHECK_EQ(parse("-c h -t 30 -r -p 6000 -l 256 -w 512 -i 0 -o x.txt", &o, err), 0, "all client options");
    CHECK_EQ(o.seconds, 30, "-t 30");
    CHECK_EQ(o.reverse, 1, "-r");
    CHECK_EQ(o.port, 6000, "-p 6000");
    CHECK_EQ(o.lenKb, 256, "-l 256");
    CHECK_EQ(o.winKb, 512, "-w 512");
    CHECK_EQ(o.interval, 0, "-i 0");
    CHECK(strcmp(o.out, "x.txt") == 0, "-o");

    CHECK_EQ(parse("/C h /N 100", &o, err), 0, "slash and upper case");
    CHECK_EQ(o.megabytes, 100, "-n 100");
    CHECK_EQ(o.seconds, 0, "-n clears the time");

    CHECK_EQ(parse("-s -1 -p 5001", &o, err), 0, "server once");
    CHECK_EQ(o.once, 1, "-1");

    CHECK_EQ(parse("-h", &o, err), 1, "-h is help");
    CHECK_EQ(parse("-c h -?", &o, err), 1, "-? is help");

    CHECK_EQ(parse("", &o, err), -1, "no mode refused");
    CHECK(strlen(err) > 0 && strlen(err) <= 78, "refusal text fits");
    CHECK_EQ(parse("-s -c h", &o, err), -1, "both modes refused");
    CHECK_EQ(parse("-c", &o, err), -1, "-c without an address");
    CHECK_EQ(parse("-c -t", &o, err), -1, "-c followed by an option");
    CHECK_EQ(parse("-s -r", &o, err), -1, "-r on the server refused");
    CHECK_EQ(parse("-s -t 5", &o, err), -1, "-t on the server refused");
    CHECK_EQ(parse("-c h -1", &o, err), -1, "-1 on the client refused");
    CHECK_EQ(parse("-c h -t 5 -n 5", &o, err), -1, "-t and -n refused");
    CHECK_EQ(parse("-c h -t 0", &o, err), -1, "-t 0 refused");
    CHECK_EQ(parse("-c h -t 3601", &o, err), -1, "-t over the limit");
    CHECK_EQ(parse("-c h -t 3600", &o, err), 0, "-t at the limit");
    CHECK_EQ(parse("-c h -p 65536", &o, err), -1, "port over 65535");
    CHECK_EQ(parse("-c h -p 99999999999999999999", &o, err), -1, "number overflow");
    CHECK_EQ(parse("-c h -p 12a", &o, err), -1, "not a number");
    CHECK_EQ(parse("-c h -l 1025", &o, err), -1, "-l over 1024");
    CHECK_EQ(parse("-c h -w 0", &o, err), -1, "-w 0 refused");
    CHECK_EQ(parse("-c h -i 61", &o, err), -1, "-i over 60");
    CHECK_EQ(parse("-c h -t", &o, err), -1, "-t without a value");
    CHECK_EQ(parse("-c h -x", &o, err), -1, "unknown option");
    CHECK_EQ(parse("-c h extra", &o, err), -1, "stray word");
    CHECK_EQ(parse("-c h --t", &o, err), -1, "long option refused");
    CHECK_EQ(parse("-c h -o", &o, err), -1, "-o without a name");
}

static void test_blocks(void)
{
    unsigned char b[TBS_BLOCK];
    TBS_REQ r, g;
    TBS_RES s, t;
    static const unsigned char want[TBS_BLOCK] = {
        'T', 'B', 'S', '1', 1, 1, 0, 0, 0, 0, 0, 30, 0, 0, 0, 0
    };

    r.dir = TBS_DIR_SERVER_SENDS;
    r.seconds = 30;
    r.megabytes = 0;
    tbs_req_put(b, &r);
    CHECK(memcmp(b, want, TBS_BLOCK) == 0, "request bytes");
    CHECK_EQ(tbs_req_get(b, &g), 0, "request read back");
    CHECK_EQ(g.dir, TBS_DIR_SERVER_SENDS, "dir back");
    CHECK_EQ(g.seconds, 30, "seconds back");

    r.dir = TBS_DIR_CLIENT_SENDS;
    r.seconds = 0;
    r.megabytes = 0x01020304;
    tbs_req_put(b, &r);
    CHECK_EQ(b[5], 0, "client sends");
    CHECK_EQ(b[12], 1, "MB big-endian");
    CHECK_EQ(b[15], 4, "MB low byte");
    CHECK_EQ(tbs_req_get(b, &g), -1, "MB over the limit refused");
    r.megabytes = 1000;
    tbs_req_put(b, &r);
    CHECK_EQ(tbs_req_get(b, &g), 0, "MB read");
    CHECK_EQ(g.megabytes, 1000, "MB back");

    memcpy(b, want, TBS_BLOCK);
    b[0] = 'X';
    CHECK_EQ(tbs_req_get(b, &g), -1, "bad magic");
    memcpy(b, want, TBS_BLOCK);
    b[4] = 2;
    CHECK_EQ(tbs_req_get(b, &g), -1, "bad version");
    memcpy(b, want, TBS_BLOCK);
    b[5] = 2;
    CHECK_EQ(tbs_req_get(b, &g), -1, "bad direction");
    memcpy(b, want, TBS_BLOCK);
    b[11] = 0;
    CHECK_EQ(tbs_req_get(b, &g), -1, "neither time nor amount");
    memcpy(b, want, TBS_BLOCK);
    b[15] = 1;
    CHECK_EQ(tbs_req_get(b, &g), -1, "both time and amount");
    memcpy(b, want, TBS_BLOCK);
    b[10] = 0x0E;
    b[11] = 0x11;
    CHECK_EQ(tbs_req_get(b, &g), -1, "3601 s refused");

    s.bytes = 5000000000.0 + 7.0;
    s.ms = 30012;
    tbs_res_put(b, &s);
    CHECK(memcmp(b, "TBSR", 4) == 0, "result magic");
    CHECK_EQ(b[7], 1, "bytes high word");
    CHECK_EQ(tbs_res_get(b, &t), 0, "result read back");
    CHECK(t.bytes == s.bytes, "bytes back exactly past 4 GB");
    CHECK_EQ(t.ms, 30012, "ms back");
    b[3] = '1';
    CHECK_EQ(tbs_res_get(b, &t), -1, "a request is not a result");
}

static void test_figures(void)
{
    char f[96];
    CHECK(tbs_mbits(1250000.0, 1000) == 10.0, "1.25 MB in 1 s = 10 Mbit/s");
    CHECK(tbs_mbits(1.0, 0) == 0.0, "0 ms gives 0");
    tbs_figure(f, sizeof(f), 125000000.0, 10000);
    CHECK(strcmp(f, "   125.00 MB in  10.00 s =   100.00 Mbit/s") == 0, "figure text");
    tbs_figure(f, 10, 125000000.0, 10000);
    CHECK_EQ(strlen(f), 9, "figure cut to the buffer");
    tbs_figure(f, sizeof(f), 1e12, 1);
    CHECK(strlen(f) < 79, "largest figure fits a line");
}

int main(void)
{
    test_cmdline();
    test_blocks();
    test_figures();
    printf("test_tbs: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
