/*
 * tbs_core.h - TBSPEED's pure half: the command line, the 16-byte request
 * and result blocks both ends exchange, and the text of every figure. No
 * Windows call: test\test_tbs.c links it on the host. C89.
 *
 * TBSPEED is a developer diagnostic (never in a package): a TCP throughput
 * reading between two computers with no thread at all, so that it starts on
 * a Windows 98 SE machine where a second thread cannot be created.
 */

#ifndef TBSPEED_TBS_CORE_H
#define TBSPEED_TBS_CORE_H

#include <stddef.h>

#define TBS_VERSION_TEXT    "1.0"
#define TBS_PORT            5102
#define TBS_BLOCK           16          /* request and result, bytes */
#define TBS_DEF_SECONDS     10
#define TBS_MAX_SECONDS     3600
#define TBS_MAX_MB          1000000L
#define TBS_DEF_LEN_KB      64
#define TBS_MAX_LEN_KB      1024
#define TBS_MAX_WIN_KB      8192
#define TBS_MAX_INTERVAL    60
#define TBS_HOST_MAX        255
#define TBS_PATH_MAX        259

#define TBS_MODE_NONE       0
#define TBS_MODE_SERVER     1
#define TBS_MODE_CLIENT     2

/* direction in the request: who sends the data */
#define TBS_DIR_CLIENT_SENDS 0
#define TBS_DIR_SERVER_SENDS 1

typedef struct tbs_opts {
    int mode;
    char host[TBS_HOST_MAX + 1];
    unsigned port;
    unsigned long seconds;      /* 0 when megabytes is set */
    unsigned long megabytes;    /* 0 when timed */
    int reverse;                /* -r: the server sends */
    unsigned lenKb;             /* -l: buffer, KB */
    unsigned winKb;             /* -w: socket buffers, KB; 0 = system's */
    unsigned interval;          /* -i: seconds between progress lines; 0 = none */
    int once;                   /* -1: the server serves one test */
    char out[TBS_PATH_MAX + 1]; /* -o: results also appended here */
} TBS_OPTS;

typedef struct tbs_req {
    int dir;
    unsigned long seconds;
    unsigned long megabytes;
} TBS_REQ;

typedef struct tbs_res {
    double bytes;
    unsigned long ms;
} TBS_RES;

/* 0 = run; 1 = help asked; -1 = refused, err holds why (<= 78 columns) */
int tbs_parse(int argc, char **argv, TBS_OPTS *o, char *err, size_t errLen);

void tbs_req_put(unsigned char *b, const TBS_REQ *r);
/* 0 = a valid request; -1 = not TBSPEED's or out of range */
int tbs_req_get(const unsigned char *b, TBS_REQ *r);

void tbs_res_put(unsigned char *b, const TBS_RES *r);
int tbs_res_get(const unsigned char *b, TBS_RES *r);

/* Mbit/s (10^6 bits a second); 0 when ms is 0 */
double tbs_mbits(double bytes, unsigned long ms);

/*
 * One figure line, e.g. "   117.42 MB in  10.01 s =    98.41 Mbit/s":
 * MB are 10^6 bytes. buf holds at least 64 bytes.
 */
void tbs_figure(char *buf, size_t len, double bytes, unsigned long ms);

#endif
