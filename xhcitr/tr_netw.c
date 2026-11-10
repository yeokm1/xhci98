/*
 * tr_netw.c - the half of XHCITR's "Thunderbolt networking" nodes that
 * calls Windows (tr_net.h; design record 35 section 17, task 48.18): the
 * request to xhcicm.sys's query device, the adapter's IP addresses, and
 * the snapshot's section. C89.
 *
 * THE IP ADDRESS is the network stack's, not the drivers': GetAdaptersInfo
 * of iphlpapi.dll, looked up at run time (LoadLibrary, GetProcAddress) so
 * the import gate's lists are unchanged and a system without the DLL
 * still runs the tool. MSVC 6.0's headers have no iptypes.h, so the two
 * structures are restated below from their documented layout (Microsoft
 * Learn, "IP_ADAPTER_INFO structure" and "IP_ADDR_STRING structure"), and
 * only their leading fields are read: each entry is reached through its
 * own Next pointer, so the trailing lease times, whose width differs
 * between SDKs, are never addressed.
 */

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "tr_net.h"

typedef struct _TRN_IP_ADDR {
    struct _TRN_IP_ADDR *Next;
    char IpAddress[16];
    char IpMask[16];
    DWORD Context;
} TRN_IP_ADDR;

typedef struct _TRN_ADAPTER {
    struct _TRN_ADAPTER *Next;
    DWORD ComboIndex;
    char AdapterName[256 + 4];
    char Description[128 + 4];
    UINT AddressLength;
    BYTE Address[8];
    DWORD Index;
    UINT Type;
    UINT DhcpEnabled;
    TRN_IP_ADDR *CurrentIpAddress;
    TRN_IP_ADDR IpAddressList;
} TRN_ADAPTER;

typedef DWORD (WINAPI *PFN_TRN_GETADAPTERSINFO)(TRN_ADAPTER *, PULONG);

#define TRN_ERROR_BUFFER_OVERFLOW   111UL
#define TRN_ERROR_NO_DATA           232UL
#define TRN_INFO_MAX                (256UL * 1024UL)

static HANDLE trn_open(const char *qname, int nt)
{
    char path[64];
    HANDLE h;

    if (qname[0] == '\\') {
        tr_copy(path, qname, sizeof(path));
    } else {
        tr_sprintf(path, sizeof(path), "\\\\.\\%s", qname);
    }
    h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE && !nt) {
        /* 98 SE and ME: a WDM device name may want an access mask; the
         * request stays FILE_ANY_ACCESS and nothing is written. */
        h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        OPEN_EXISTING, 0, NULL);
    }
    return h;
}

int tr_net_read(const char *qname, int nt, unsigned char **buf, TR_QNET *q,
                unsigned long *err)
{
    HANDLE h;
    DWORD size = CM_QN_ASK_BYTES;
    DWORD got;
    int tries;
    int r = -1;

    *buf = NULL;
    *err = 0;
    memset(q, 0, sizeof(*q));
    if (qname == NULL || qname[0] == '\0') {
        return -1;
    }
    h = trn_open(qname, nt);
    if (h == INVALID_HANDLE_VALUE) {
        *err = GetLastError();
        return -1;
    }
    for (tries = 0; tries < 3; tries++) {
        unsigned char *b = (unsigned char *)realloc(*buf, size);
        if (b == NULL) {
            *err = ERROR_OUTOFMEMORY;
            r = -1;
            break;
        }
        *buf = b;
        got = 0;
        if (!DeviceIoControl(h, CM_QN_IOCTL, NULL, 0, b, size, &got, NULL)) {
            *err = GetLastError();
            r = (*err == ERROR_INVALID_FUNCTION || *err == ERROR_NOT_SUPPORTED) ? 0 : -1;
            break;
        }
        if (got > size) {
            r = -1;
            break;
        }
        r = tr_qnet_parse(b, got, q);
        if (r == -1 && q->Bytes > size && q->Bytes <= 65536UL) {
            size = q->Bytes;
            continue;
        }
        r = r == 1 ? 1 : -1;
        break;
    }
    CloseHandle(h);
    if (r != 1) {
        free(*buf);
        *buf = NULL;
        memset(q, 0, sizeof(*q));
    }
    return r;
}

void tr_net_ip(const unsigned char *mac6, char *out, size_t cap)
{
    HMODULE dll;
    PFN_TRN_GETADAPTERSINFO get;
    TRN_ADAPTER *list = NULL;
    TRN_ADAPTER *a;
    const TRN_IP_ADDR *ip;
    ULONG size = 16384UL;
    DWORD rc = 0;
    size_t n;
    int tries;
    int found = 0;

    out[0] = '\0';
    dll = LoadLibraryA("iphlpapi.dll");
    if (dll == NULL) {
        tr_copy(out, "not read (this Windows has no IP Helper)", cap);
        return;
    }
    get = (PFN_TRN_GETADAPTERSINFO)GetProcAddress(dll, "GetAdaptersInfo");
    if (get == NULL) {
        FreeLibrary(dll);
        tr_copy(out, "not read (this Windows has no GetAdaptersInfo)", cap);
        return;
    }
    for (tries = 0; tries < 3; tries++) {
        TRN_ADAPTER *b = (TRN_ADAPTER *)realloc(list, size);
        if (b == NULL) {
            rc = ERROR_OUTOFMEMORY;
            break;
        }
        list = b;
        rc = get(list, &size);
        if (rc != TRN_ERROR_BUFFER_OVERFLOW || size > TRN_INFO_MAX) {
            break;
        }
    }
    if (rc == TRN_ERROR_NO_DATA) {
        tr_copy(out, "none (Windows lists no adapter)", cap);
    } else if (rc != 0) {
        tr_sprintf(out, cap, "not read (error %lu)", (unsigned long)rc);
    } else {
        for (a = list; a != NULL && !found; a = a->Next) {
            if (a->AddressLength != 6 || memcmp(a->Address, mac6, 6) != 0) {
                continue;
            }
            found = 1;
            for (ip = &a->IpAddressList; ip != NULL; ip = ip->Next) {
                char addr[17];
                char mask[17];

                memcpy(addr, ip->IpAddress, 16);
                addr[16] = '\0';
                memcpy(mask, ip->IpMask, 16);
                mask[16] = '\0';
                if (addr[0] == '\0' || strcmp(addr, "0.0.0.0") == 0) {
                    continue;
                }
                n = strlen(out);
                tr_sprintf(out + n, cap - n, "%s%s (%s)", n ? ", " : "", addr,
                           mask);
            }
            if (out[0] == '\0') {
                tr_copy(out, "none yet", cap);
            }
        }
        if (!found) {
            tr_copy(out, "none (Windows lists no adapter with this MAC)", cap);
        }
    }
    free(list);
    FreeLibrary(dll);
}

void tr_net_dev_text(TR_TEXT *t, int nt)
{
    char qname[16];
    unsigned char *buf;
    unsigned long err;
    TR_QNET q;
    int any = 0;
    int k;
    int r;

    for (k = 0; k < (int)CM_Q_MAX_DEVICES; k++) {
        tr_sprintf(qname, sizeof(qname), "XHCICM%d", k);
        r = tr_net_read(qname, nt, &buf, &q, &err);
        if (r == 0) {
            tr_printf(t, "  %s: not served by this xhcicm.sys (older than "
                         "this tool)\n", qname);
            any = 1;
            continue;
        }
        if (r != 1) {
            continue;
        }
        any = 1;
        tr_printf(t, "  %s:\n", qname);
        if (!tr_qnet_show(&q)) {
            tr_put(t, "  Thunderbolt networking: off\n");
        } else {
            tr_qnet_text(t, &q, -1, NULL, 1);
        }
        tr_put(t, "\n");
        free(buf);
    }
    if (!any) {
        tr_put(t, "  none answered\n");
    }
}
