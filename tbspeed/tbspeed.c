/*
 * tbspeed.c - TBSPEED.EXE, a single-threaded TCP throughput reading for
 * Windows 98 SE to Windows 11 (a developer diagnostic, never in a package).
 *
 *   TBSPEED -s [-p port] [-1] [-w KB] [-l KB] [-i sec] [-o file]
 *   TBSPEED -c <address> [-p port] [-t sec | -n MB] [-r] [-w KB] [-l KB]
 *           [-i sec] [-o file]
 *
 * The -c side connects to the -s side and sends a 16-byte request naming
 * who sends and for how long; with -r the -s side sends and the -c side
 * receives on the same connection, so only the -s side ever accepts a
 * connection. The receiver counts from its first byte to the sender's close
 * and sends its count back, so both ends print the receiver's figure.
 *
 * No thread is created: blocking Winsock 1.1 calls and select() alone, the
 * single-threaded static C runtime (LIBC), and imports from KERNEL32 and
 * WSOCK32 only, so that it starts on a Windows 98 SE machine where
 * CreateThread fails.
 */

#include <windows.h>
#include <winsock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "tbs_core.h"

#ifndef SD_SEND
#define SD_SEND 1
#endif

#define IDLE_LIMIT_SEC  30
#define REQ_WAIT_SEC    10
#define RES_WAIT_SEC    30

static FILE *g_log;
static LARGE_INTEGER g_freq;
static int g_qpc;

static void out(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
    if (g_log != NULL) {
        va_start(ap, fmt);
        vfprintf(g_log, fmt, ap);
        va_end(ap);
        fflush(g_log);
    }
}

static double now_ms(void)
{
    if (g_qpc) {
        LARGE_INTEGER c;
        if (QueryPerformanceCounter(&c))
            return (double)c.QuadPart * 1000.0 / (double)g_freq.QuadPart;
    }
    return (double)GetTickCount();
}

static unsigned long span(double t0, double t1)
{
    double d = t1 - t0;
    if (d < 0.0)
        d = 0.0;
    return (unsigned long)(d + 0.5);
}

static const char *wsa_text(int e)
{
    switch (e) {
    case WSAECONNREFUSED: return "connection refused (is TBSPEED -s running there?)";
    case WSAETIMEDOUT:    return "timed out";
    case WSAEHOSTUNREACH: return "no route to that address";
    case WSAENETUNREACH:  return "network unreachable";
    case WSAECONNRESET:   return "connection reset by the other computer";
    case WSAECONNABORTED: return "connection aborted";
    case WSAEADDRINUSE:   return "the port is in use";
    case WSAEACCES:       return "access denied";
    case WSAHOST_NOT_FOUND: return "address not found";
    default:              return "Winsock error";
    }
}

static void wsa_fail(const char *what)
{
    int e = WSAGetLastError();
    out("%s failed: %s (%d)\n", what, wsa_text(e), e);
}

/*
 * The connected socket is non-blocking, so a peer that stops reading or
 * writing cannot hold a send or a receive past the limits below.
 */
static int nonblocking(SOCKET s)
{
    u_long on = 1;
    if (ioctlsocket(s, FIONBIO, &on) == SOCKET_ERROR) {
        wsa_fail("Setting up the connection");
        return -1;
    }
    return 0;
}

/* 1 = ready (wr: writable, else readable), 0 = timed out, -1 = error */
static int wait_sock(SOCKET s, int wr, unsigned sec)
{
    fd_set f;
    struct timeval tv;
    int n;
    FD_ZERO(&f);
    FD_SET(s, &f);
    tv.tv_sec = (long)sec;
    tv.tv_usec = 0;
    n = select(0, wr ? NULL : &f, wr ? &f : NULL, NULL, &tv);
    if (n == SOCKET_ERROR)
        return -1;
    return n > 0 ? 1 : 0;
}

/* read exactly len bytes within sec seconds; 0 = done */
static int recv_block(SOCKET s, unsigned char *b, int len, unsigned sec,
                      const char *what)
{
    int got = 0;
    double t0 = now_ms();
    while (got < len) {
        int n;
        if (now_ms() - t0 >= (double)sec * 1000.0) {
            out("%s: nothing came within %u s\n", what, sec);
            return -1;
        }
        n = wait_sock(s, 0, 1);
        if (n < 0) {
            wsa_fail(what);
            return -1;
        }
        if (n == 0)
            continue;
        n = recv(s, (char *)b + got, len - got, 0);
        if (n == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEWOULDBLOCK)
                continue;
            wsa_fail(what);
            return -1;
        }
        if (n == 0) {
            out("%s: the other computer closed the connection\n", what);
            return -1;
        }
        got += n;
    }
    return 0;
}

/* send exactly len bytes within sec seconds; 0 = done */
static int send_block(SOCKET s, const unsigned char *b, int len, unsigned sec,
                      const char *what)
{
    int sent = 0;
    double t0 = now_ms();
    while (sent < len) {
        int n;
        if (now_ms() - t0 >= (double)sec * 1000.0) {
            out("%s: the other computer took nothing for %u s\n", what, sec);
            return -1;
        }
        n = wait_sock(s, 1, 1);
        if (n < 0) {
            wsa_fail(what);
            return -1;
        }
        if (n == 0)
            continue;
        n = send(s, (const char *)b + sent, len - sent, 0);
        if (n == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEWOULDBLOCK)
                continue;
            wsa_fail(what);
            return -1;
        }
        sent += n;
    }
    return 0;
}

static void progress(unsigned long from, unsigned long to, double bytes,
                     unsigned long ms)
{
    char f[96];
    tbs_figure(f, sizeof(f), bytes, ms);
    out("  %4lu-%4lu s %s\n", from, to, f);
}

/*
 * The sending half: until the time or the amount is reached. A timed test
 * ends on time even when the receiver has stopped taking data; a peer that
 * takes nothing for IDLE_LIMIT_SEC fails the test either way.
 */
static int do_send(SOCKET s, char *buf, int len, unsigned long seconds,
                   unsigned long megabytes, unsigned interval, TBS_RES *r)
{
    double t0 = now_ms(), tLast = t0, bLast = 0.0, tData = t0;
    double limitMs = (double)seconds * 1000.0;
    double limitB = (double)megabytes * 1000000.0;
    unsigned long nextTick = interval;
    int rc = 0;

    r->bytes = 0.0;
    for (;;) {
        int want = len, n;
        double t;
        if (megabytes != 0) {
            double left = limitB - r->bytes;
            if (left <= 0.0)
                break;
            if (left < (double)want)
                want = (int)left;
        }
        n = wait_sock(s, 1, 1);
        if (n < 0) {
            wsa_fail("Sending");
            rc = -1;
            break;
        }
        if (n > 0) {
            n = send(s, buf, want, 0);
            if (n == SOCKET_ERROR) {
                if (WSAGetLastError() != WSAEWOULDBLOCK) {
                    wsa_fail("Sending");
                    rc = -1;
                    break;
                }
                n = 0;
                Sleep(10);
            }
        }
        t = now_ms();
        if (n > 0) {
            tData = t;
            r->bytes += (double)n;
        } else if (t - tData >= (double)IDLE_LIMIT_SEC * 1000.0) {
            out("Sending: the other computer took nothing for %u s\n",
                IDLE_LIMIT_SEC);
            rc = -1;
            break;
        }
        if (interval != 0 && t - t0 >= (double)nextTick * 1000.0) {
            progress(nextTick - interval, nextTick, r->bytes - bLast,
                     span(tLast, t));
            tLast = t;
            bLast = r->bytes;
            nextTick += interval;
        }
        if (megabytes == 0 && t - t0 >= limitMs)
            break;
    }
    r->ms = span(t0, now_ms());
    return rc;
}

/* the receiving half: from the first byte to the sender's close */
static int do_recv(SOCKET s, char *buf, int len, unsigned interval, TBS_RES *r)
{
    double t0 = 0.0, tLast = 0.0, bLast = 0.0, tEnd;
    unsigned long nextTick = interval;
    unsigned idle = 0;
    int started = 0, rc = 0;

    r->bytes = 0.0;
    for (;;) {
        int n = wait_sock(s, 0, 1);
        double t;
        if (n < 0) {
            wsa_fail("Receiving");
            rc = -1;
            break;
        }
        if (n == 0) {
            if (++idle >= IDLE_LIMIT_SEC) {
                out("Receiving: nothing came for %u s\n", IDLE_LIMIT_SEC);
                rc = -1;
                break;
            }
        } else {
            n = recv(s, buf, len, 0);
            if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
                n = -1;
            else if (n == SOCKET_ERROR) {
                wsa_fail("Receiving");
                rc = -1;
                break;
            }
            if (n == 0)
                break;
            if (n < 0) {
                Sleep(10);
                continue;
            }
            idle = 0;
            if (!started) {
                started = 1;
                t0 = now_ms();
                tLast = t0;
            }
            r->bytes += (double)n;
        }
        if (started && interval != 0) {
            t = now_ms();
            if (t - t0 >= (double)nextTick * 1000.0) {
                progress(nextTick - interval, nextTick, r->bytes - bLast,
                         span(tLast, t));
                tLast = t;
                bLast = r->bytes;
                nextTick += interval;
            }
        }
    }
    tEnd = now_ms();
    r->ms = started ? span(t0, tEnd) : 0;
    return rc;
}

static void set_buffers(SOCKET s, unsigned winKb)
{
    int v;
    if (winKb == 0)
        return;
    v = (int)winKb * 1024;
    if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char *)&v, sizeof(v)) == SOCKET_ERROR)
        wsa_fail("Setting the send buffer");
    if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char *)&v, sizeof(v)) == SOCKET_ERROR)
        wsa_fail("Setting the receive buffer");
}

static void print_figure(const char *label, const TBS_RES *r)
{
    char f[96];
    tbs_figure(f, sizeof(f), r->bytes, r->ms);
    out("%s%s\n", label, f);
}

/*
 * One test on a connected socket. sends = this end sends. The receiver
 * returns its count; the sender waits for it after closing its half.
 */
static int run_test(SOCKET s, int sends, unsigned long seconds,
                    unsigned long megabytes, const TBS_OPTS *o, char *buf)
{
    TBS_RES mine, theirs;
    unsigned char b[TBS_BLOCK];
    int len = (int)o->lenKb * 1024;

    if (sends) {
        int rc = do_send(s, buf, len, seconds, megabytes, o->interval, &mine);
        if (shutdown(s, SD_SEND) == SOCKET_ERROR && rc == 0) {
            wsa_fail("Closing the send half");
            rc = -1;
        }
        print_figure("Sent:     ", &mine);
        if (rc != 0)
            return -1;
        if (recv_block(s, b, TBS_BLOCK, RES_WAIT_SEC, "Waiting for the receiver's count") != 0)
            return -1;
        if (tbs_res_get(b, &theirs) != 0) {
            out("The receiver's count was not TBSPEED's\n");
            return -1;
        }
        print_figure("Received: ", &theirs);
        return 0;
    } else {
        int rc = do_recv(s, buf, len, o->interval, &mine);
        print_figure("Received: ", &mine);
        if (rc != 0)
            return -1;
        if (megabytes != 0 && mine.bytes != (double)megabytes * 1000000.0) {
            out("The sender stopped before %lu MB\n", megabytes);
            rc = -1;
        } else if (mine.bytes == 0.0) {
            out("Nothing was received\n");
            rc = -1;
        }
        tbs_res_put(b, &mine);
        if (send_block(s, b, TBS_BLOCK, RES_WAIT_SEC, "Sending the count back") != 0)
            return -1;
        shutdown(s, SD_SEND);
        return rc;
    }
}

static void show_addresses(void)
{
    char name[256];
    struct hostent *h;
    int i;
    if (gethostname(name, sizeof(name)) != 0)
        return;
    h = gethostbyname(name);
    if (h == NULL || h->h_addrtype != AF_INET)
        return;
    out("This computer's addresses:");
    for (i = 0; h->h_addr_list[i] != NULL; i++) {
        struct in_addr a;
        memcpy(&a, h->h_addr_list[i], sizeof(a));
        out("  %s", inet_ntoa(a));
    }
    out("\n");
}

static int server(const TBS_OPTS *o, char *buf)
{
    SOCKET ls;
    struct sockaddr_in sa;
    int rc = 0;

    ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls == INVALID_SOCKET) {
        wsa_fail("Opening a socket");
        return 1;
    }
    set_buffers(ls, o->winKb);
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons((unsigned short)o->port);
    if (bind(ls, (struct sockaddr *)&sa, sizeof(sa)) == SOCKET_ERROR) {
        wsa_fail("Opening the port");
        closesocket(ls);
        return 1;
    }
    if (listen(ls, 1) == SOCKET_ERROR) {
        wsa_fail("Listening");
        closesocket(ls);
        return 1;
    }
    show_addresses();
    out("TBSPEED %s waiting on port %u. Press Ctrl+C to stop.\n",
        TBS_VERSION_TEXT, o->port);

    for (;;) {
        struct sockaddr_in peer;
        int plen = sizeof(peer);
        unsigned char b[TBS_BLOCK];
        TBS_REQ rq;
        SOCKET s;
        int w;
        /*
         * Waiting in select a second at a time, not in accept, keeps the
         * program answering Ctrl+C and Windows 98's close while idle.
         */
        while ((w = wait_sock(ls, 0, 1)) == 0)
            ;
        if (w < 0) {
            wsa_fail("Waiting for a connection");
            closesocket(ls);
            return 1;
        }
        s = accept(ls, (struct sockaddr *)&peer, &plen);
        if (s == INVALID_SOCKET) {
            wsa_fail("Accepting");
            closesocket(ls);
            return 1;
        }
        out("\nConnection from %s\n", inet_ntoa(peer.sin_addr));
        rc = 1;
        if (nonblocking(s) == 0 &&
            recv_block(s, b, TBS_BLOCK, REQ_WAIT_SEC, "Reading the request") == 0) {
            if (tbs_req_get(b, &rq) != 0) {
                out("Not a TBSPEED request; closed\n");
            } else {
                int sends = (rq.dir == TBS_DIR_SERVER_SENDS);
                if (rq.megabytes != 0)
                    out("Test: %s, %lu MB\n", sends ? "this computer sends" :
                        "this computer receives", rq.megabytes);
                else
                    out("Test: %s, %lu s\n", sends ? "this computer sends" :
                        "this computer receives", rq.seconds);
                if (run_test(s, sends, rq.seconds, rq.megabytes, o, buf) == 0)
                    rc = 0;
            }
        }
        closesocket(s);
        if (o->once)
            break;
        out("Waiting for the next test.\n");
    }
    closesocket(ls);
    return rc;
}

static int client(const TBS_OPTS *o, char *buf)
{
    SOCKET s;
    struct sockaddr_in sa;
    unsigned long ip;
    unsigned char b[TBS_BLOCK];
    TBS_REQ rq;
    int rc;

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)o->port);
    ip = inet_addr(o->host);
    if (ip == INADDR_NONE) {
        struct hostent *h = gethostbyname(o->host);
        if (h == NULL || h->h_addrtype != AF_INET) {
            wsa_fail("Looking up the address");
            return 1;
        }
        memcpy(&sa.sin_addr, h->h_addr_list[0], sizeof(sa.sin_addr));
    } else {
        sa.sin_addr.s_addr = ip;
    }

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) {
        wsa_fail("Opening a socket");
        return 1;
    }
    set_buffers(s, o->winKb);
    out("Connecting to %s port %u...\n", inet_ntoa(sa.sin_addr), o->port);
    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) == SOCKET_ERROR) {
        wsa_fail("Connecting");
        closesocket(s);
        return 1;
    }
    if (nonblocking(s) != 0) {
        closesocket(s);
        return 1;
    }
    rq.dir = o->reverse ? TBS_DIR_SERVER_SENDS : TBS_DIR_CLIENT_SENDS;
    rq.seconds = o->seconds;
    rq.megabytes = o->megabytes;
    tbs_req_put(b, &rq);
    if (send_block(s, b, TBS_BLOCK, REQ_WAIT_SEC, "Sending the request") != 0) {
        closesocket(s);
        return 1;
    }
    if (o->megabytes != 0)
        out("Test: %s, %lu MB\n", o->reverse ? "the other computer sends" :
            "this computer sends", o->megabytes);
    else
        out("Test: %s, %lu s\n", o->reverse ? "the other computer sends" :
            "this computer sends", o->seconds);
    rc = run_test(s, !o->reverse, o->seconds, o->megabytes, o, buf);
    closesocket(s);
    return rc == 0 ? 0 : 1;
}

static void usage(void)
{
    printf("TBSPEED %s - TCP throughput between two computers, no threads\n\n", TBS_VERSION_TEXT);
    printf("  TBSPEED -s [-p port] [-1] [options]       wait for the other computer\n");
    printf("  TBSPEED -c <address> [-t sec | -n MB] [-r] [options]\n");
    printf("                                            connect and run one test\n\n");
    printf("  -r        reverse: the -s side sends, this side receives\n");
    printf("  -t sec    send for this long (default %d)\n", TBS_DEF_SECONDS);
    printf("  -n MB     send this many MB (10^6 bytes) instead\n");
    printf("  -p port   TCP port (default %d)\n", TBS_PORT);
    printf("  -l KB     buffer per send/receive call (default %d)\n", TBS_DEF_LEN_KB);
    printf("  -w KB     socket buffer size (default: the system's)\n");
    printf("  -i sec    progress every this many seconds, 0 = none (default 1)\n");
    printf("  -1        -s: stop after one test\n");
    printf("  -o file   also append what is shown to this file\n\n");
    printf("Mbit/s are 10^6 bits a second, measured by the receiver.\n");
}

int main(int argc, char **argv)
{
    TBS_OPTS o;
    char err[96];
    WSADATA wd;
    char *buf;
    int rc, i;

    rc = tbs_parse(argc, argv, &o, err, sizeof(err));
    if (rc == 1) {
        usage();
        return 0;
    }
    if (rc != 0) {
        printf("%s\nTBSPEED -h shows the options.\n", err);
        return 2;
    }
    if (o.out[0] != '\0') {
        g_log = fopen(o.out, "a");
        if (g_log == NULL) {
            printf("Cannot open %s\n", o.out);
            return 1;
        }
    }
    g_qpc = QueryPerformanceFrequency(&g_freq) && g_freq.QuadPart != 0;
    if (WSAStartup(MAKEWORD(1, 1), &wd) != 0) {
        out("Winsock did not start\n");
        return 1;
    }
    buf = (char *)malloc((size_t)o.lenKb * 1024);
    if (buf == NULL) {
        out("Out of memory\n");
        WSACleanup();
        return 1;
    }
    for (i = 0; i < (int)o.lenKb * 1024; i++)
        buf[i] = (char)(i * 7 + (i >> 8));

    if (o.mode == TBS_MODE_SERVER)
        rc = server(&o, buf);
    else
        rc = client(&o, buf);

    free(buf);
    WSACleanup();
    if (g_log != NULL)
        fclose(g_log);
    return rc;
}
