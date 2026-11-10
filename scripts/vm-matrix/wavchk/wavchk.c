/*
 * wavchk.c - WAVCHK.EXE, the release legs' playback check (build-and-test.md,
 * "Faster emulator legs"; GitHub issue 32, docs/issues/30). It plays a PCM
 * WAV file through the first waveOut device whose name holds a given text,
 * waiting for Windows to install that device first, and writes each call's
 * MMSYSERR result and the time the play took to standard output.
 *
 *   WAVCHK <file.wav> [/name <text>] [/wait <seconds>]
 *
 * /name defaults to USB, /wait (how long to wait for the device) to 300.
 * Exit codes: 0 the file played to the end; 1 the arguments or the file
 * refused; 2 no such device within the wait; 3 a waveOut call failed (the
 * open is tried again for 30 s once the device is there, since Windows may
 * still be finishing its install); 4 the play did not end in twice its
 * length plus 10 s, or ended in less than nine tenths of its length.
 *
 * Built by build.cmd (MSVC 6.0). Never in a package.
 */

#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define WC_MAX_FILE   (4UL * 1024UL * 1024UL)
#define WC_OPEN_TRIES 30

static unsigned long wcLe32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static int wcHas(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    size_t i;
    size_t j;

    if (n == 0) {
        return 1;
    }
    for (i = 0; hay[i] != '\0'; i++) {
        for (j = 0; j < n; j++) {
            if (hay[i + j] == '\0' ||
                toupper((unsigned char)hay[i + j]) !=
                toupper((unsigned char)needle[j])) {
                break;
            }
        }
        if (j == n) {
            return 1;
        }
    }
    return 0;
}

static void wcSay(const char *call, MMRESULT r)
{
    char text[MAXERRORLENGTH];

    text[0] = '\0';
    if (r != MMSYSERR_NOERROR &&
        waveOutGetErrorText(r, text, sizeof(text)) != MMSYSERR_NOERROR) {
        text[0] = '\0';
    }
    printf("WAVCHK %s MMSYSERR %u%s%s\n", call, (unsigned)r,
           text[0] ? " " : "", text);
    fflush(stdout);
}

/* The fmt and data chunks of a RIFF WAVE image; 0 when it is not PCM. */
static int wcParse(unsigned char *img, unsigned long len, WAVEFORMATEX *wfx,
                   unsigned char **data, unsigned long *dataLen)
{
    unsigned long at = 12;
    int haveFmt = 0;

    *data = NULL;
    *dataLen = 0;
    if (len < 12 || memcmp(img, "RIFF", 4) != 0 ||
        memcmp(img + 8, "WAVE", 4) != 0) {
        return 0;
    }
    while (len - at >= 8) {
        unsigned long ck = wcLe32(img + at + 4);
        unsigned char *body = img + at + 8;
        unsigned long room = len - at - 8;

        if (ck > room) {
            ck = room;
        }
        if (memcmp(img + at, "fmt ", 4) == 0 && ck >= 16) {
            memset(wfx, 0, sizeof(*wfx));
            wfx->wFormatTag = (WORD)(body[0] | (body[1] << 8));
            wfx->nChannels = (WORD)(body[2] | (body[3] << 8));
            wfx->nSamplesPerSec = wcLe32(body + 4);
            wfx->nAvgBytesPerSec = wcLe32(body + 8);
            wfx->nBlockAlign = (WORD)(body[12] | (body[13] << 8));
            wfx->wBitsPerSample = (WORD)(body[14] | (body[15] << 8));
            wfx->cbSize = 0;
            haveFmt = 1;
        } else if (memcmp(img + at, "data", 4) == 0) {
            *data = body;
            *dataLen = ck;
        }
        if (room - ck < (ck & 1UL)) {
            break;
        }
        at += 8 + ck + (ck & 1UL);
    }
    if (!haveFmt || *data == NULL || *dataLen == 0 ||
        wfx->wFormatTag != WAVE_FORMAT_PCM || wfx->nAvgBytesPerSec == 0 ||
        wfx->nBlockAlign == 0) {
        return 0;
    }
    *dataLen -= *dataLen % wfx->nBlockAlign;
    return *dataLen != 0;
}

int main(int argc, char **argv)
{
    const char *file = NULL;
    const char *name = "USB";
    unsigned long waitSec = 300;
    FILE *f;
    unsigned char *img;
    unsigned long len;
    WAVEFORMATEX wfx;
    unsigned char *data;
    unsigned long dataLen;
    unsigned long ms;
    unsigned long t;
    UINT dev = 0;
    UINT n = 0;
    UINT i;
    int found = 0;
    WAVEOUTCAPS caps;
    HWAVEOUT hwo = NULL;
    WAVEHDR hdr;
    MMRESULT r = MMSYSERR_ERROR;
    MMRESULT last;
    MMTIME mt;
    DWORD t0;
    DWORD el;
    DWORD limit;
    int rc = 0;
    int k;

    for (k = 1; k < argc; k++) {
        if (_stricmp(argv[k], "/name") == 0 && k + 1 < argc) {
            name = argv[++k];
        } else if (_stricmp(argv[k], "/wait") == 0 && k + 1 < argc) {
            waitSec = strtoul(argv[++k], NULL, 10);
        } else if (file == NULL && argv[k][0] != '/') {
            file = argv[k];
        } else {
            file = NULL;
            break;
        }
    }
    if (file == NULL || waitSec > 3600) {
        printf("WAVCHK usage: WAVCHK <file.wav> [/name <text>] "
               "[/wait <seconds, at most 3600>]\n");
        return 1;
    }

    f = fopen(file, "rb");
    if (f == NULL) {
        printf("WAVCHK cannot open %s\n", file);
        return 1;
    }
    img = (unsigned char *)malloc(WC_MAX_FILE);
    if (img == NULL) {
        fclose(f);
        printf("WAVCHK out of memory\n");
        return 1;
    }
    len = (unsigned long)fread(img, 1, WC_MAX_FILE, f);
    fclose(f);
    if (!wcParse(img, len, &wfx, &data, &dataLen)) {
        printf("WAVCHK %s is not a PCM WAV file of at most %lu bytes\n",
               file, WC_MAX_FILE);
        free(img);
        return 1;
    }
    ms = (dataLen / wfx.nAvgBytesPerSec) * 1000UL +
         ((dataLen % wfx.nAvgBytesPerSec) * 1000UL) / wfx.nAvgBytesPerSec;
    printf("WAVCHK file %s: %u Hz, %u bits, %u channels, %lu bytes, %lu ms\n",
           file, (unsigned)wfx.nSamplesPerSec, (unsigned)wfx.wBitsPerSample,
           (unsigned)wfx.nChannels, dataLen, ms);

    for (t = 0; !found; t++) {
        n = waveOutGetNumDevs();
        for (i = 0; i < n && !found; i++) {
            if (waveOutGetDevCaps(i, &caps, sizeof(caps)) ==
                    MMSYSERR_NOERROR && wcHas(caps.szPname, name)) {
                dev = i;
                found = 1;
            }
        }
        if (found || t >= waitSec) {
            break;
        }
        Sleep(1000);
    }
    if (!found) {
        printf("WAVCHK no waveOut device named *%s* in %lu s; %u devices:\n",
               name, waitSec, (unsigned)n);
        for (i = 0; i < n; i++) {
            if (waveOutGetDevCaps(i, &caps, sizeof(caps)) ==
                    MMSYSERR_NOERROR) {
                printf("WAVCHK   %u %s\n", (unsigned)i, caps.szPname);
            }
        }
        printf("WAVCHK FAIL\n");
        free(img);
        return 2;
    }
    printf("WAVCHK device %u of %u: %s (found after %lu s)\n", (unsigned)dev,
           (unsigned)n, caps.szPname, t);
    fflush(stdout);

    last = (MMRESULT)-1;
    for (k = 0; k < WC_OPEN_TRIES; k++) {
        r = waveOutOpen(&hwo, dev, &wfx, 0, 0, CALLBACK_NULL);
        if (r != last) {
            printf("WAVCHK try %d: ", k + 1);
            wcSay("waveOutOpen", r);
            last = r;
        }
        if (r == MMSYSERR_NOERROR) {
            break;
        }
        Sleep(1000);
    }
    if (r != MMSYSERR_NOERROR) {
        printf("WAVCHK waveOutOpen refused %d times, 1 s apart\n",
               WC_OPEN_TRIES);
        printf("WAVCHK FAIL\n");
        free(img);
        return 3;
    }

    memset(&hdr, 0, sizeof(hdr));
    hdr.lpData = (LPSTR)data;
    hdr.dwBufferLength = dataLen;
    r = waveOutPrepareHeader(hwo, &hdr, sizeof(hdr));
    wcSay("waveOutPrepareHeader", r);
    if (r != MMSYSERR_NOERROR) {
        rc = 3;
        goto cleanup;
    }
    t0 = GetTickCount();
    r = waveOutWrite(hwo, &hdr, sizeof(hdr));
    wcSay("waveOutWrite", r);
    if (r != MMSYSERR_NOERROR) {
        rc = 3;
        wcSay("waveOutUnprepareHeader", waveOutUnprepareHeader(hwo, &hdr,
                                                               sizeof(hdr)));
        goto cleanup;
    }
    limit = ms * 2UL + 10000UL;
    for (;;) {
        el = GetTickCount() - t0;
        if ((*(volatile DWORD *)&hdr.dwFlags & WHDR_DONE) != 0 || el > limit) {
            break;
        }
        Sleep(20);
    }
    memset(&mt, 0, sizeof(mt));
    mt.wType = TIME_BYTES;
    r = waveOutGetPosition(hwo, &mt, sizeof(mt));
    if (r == MMSYSERR_NOERROR && mt.wType == TIME_BYTES) {
        printf("WAVCHK position %lu of %lu bytes\n", (unsigned long)mt.u.cb,
               dataLen);
    } else {
        printf("WAVCHK position not read (MMSYSERR %u, type %u)\n",
               (unsigned)r, (unsigned)mt.wType);
    }
    if ((*(volatile DWORD *)&hdr.dwFlags & WHDR_DONE) == 0) {
        printf("WAVCHK not done after %lu ms (%lu ms of audio)\n",
               (unsigned long)el, ms);
        wcSay("waveOutReset", waveOutReset(hwo));
        rc = 4;
    } else {
        printf("WAVCHK played %lu ms of audio in %lu ms\n", ms,
               (unsigned long)el);
        if (el < (ms / 10UL) * 9UL) {
            printf("WAVCHK ended in less than nine tenths of its length\n");
            rc = 4;
        } else if (el > limit) {
            printf("WAVCHK ended after its bound of %lu ms\n",
                   (unsigned long)limit);
            rc = 4;
        }
    }
    r = waveOutUnprepareHeader(hwo, &hdr, sizeof(hdr));
    wcSay("waveOutUnprepareHeader", r);
    if (r != MMSYSERR_NOERROR && rc == 0) {
        rc = 3;
    }

cleanup:
    r = waveOutClose(hwo);
    wcSay("waveOutClose", r);
    if (r != MMSYSERR_NOERROR && rc == 0) {
        rc = 3;
    }
    printf("WAVCHK %s\n", rc == 0 ? "PASS" : "FAIL");
    free(img);
    return rc;
}
