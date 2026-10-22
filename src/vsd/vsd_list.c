/*
 * vsd_list.c - the pure half of S98VSD.VXD; see vsd_list.h and design
 * record 30 sections 3.3 and 4.1 to 4.3.
 *
 * Unsigned 32-bit arithmetic throughout; every sum of an address and a
 * length is checked before it is used. No file-scope static (vsd_list.h).
 */

#include "vsd_list.h"

int VsdBytes(VSD_U32 count, int chars, unsigned shift, VSD_U32 *bytes)
{
    if (chars) {
        *bytes = count;
        return 1;
    }
    if (shift >= 32 || (shift != 0 && (count >> (32 - shift)) != 0)) {
        return 0;
    }
    *bytes = count << shift;
    return 1;
}

/* n bytes from a do not end below 2^32: their exclusive end a + n wraps
 * or is 2^32 itself, so a pointer past them cannot be formed. */
int VsdWraps(VSD_U32 a, VSD_U32 n)
{
    return n != 0 && a + n <= a;
}

/* Walk the logical list once, checking it: *nfrag, *sum and *first out. */
int VsdWalk(const VSD_REQ *req, const VSD_IO *io, VSD_U32 *nfrag,
            VSD_U32 *sum, VSD_U32 *first)
{
    VSD_U32 addr = req->BufferPtr;
    VSD_U32 count;
    VSD_U32 buf;
    VSD_U32 bytes;
    VSD_U32 n = 0;
    VSD_U32 s = 0;

    *first = 0;
    for (;;) {
        if (VsdWraps(addr, 8)) {
            return VSD_R_WRAP;
        }
        io->Desc(io->Ctx, addr, &count, &buf);
        if (count == 0) {
            break;
        }
        if (n == VSD_MAX_FRAGS) {
            return VSD_R_FRAGS;
        }
        if (!VsdBytes(count, req->Chars, req->Shift, &bytes)) {
            return VSD_R_COUNT;
        }
        if (VsdWraps(buf, bytes)) {
            return VSD_R_WRAP;
        }
        if (bytes > req->MaxXfer || s > req->MaxXfer - bytes) {
            return VSD_R_MAX;
        }
        if (n == 0) {
            *first = buf;
        }
        s += bytes;
        n++;
        addr += 8;
    }
    if (n == 0) {
        return VSD_R_EMPTY;
    }
    *nfrag = n;
    *sum = s;
    return VSD_OK;
}

/* Append one fragment's runs. */
int VsdAddFrag(const VSD_IO *io, VSD_U32 buf, VSD_U32 bytes, VSD_RUN *runs,
               VSD_U32 *scratch, VSD_U32 *nruns)
{
    VSD_U32 off = buf & (VSD_PAGE - 1);
    VSD_U32 pages = (off + bytes + VSD_PAGE - 1) / VSD_PAGE;
    VSD_U32 n = *nruns;
    VSD_U32 i;
    VSD_U32 phys;
    VSD_U32 chunk;
    VSD_U32 left = bytes;

    if (pages > VSD_SCRATCH) {
        return VSD_R_SCRATCH;
    }
    for (i = 0; i < pages; i++) {
        scratch[i] = 0;
    }
    if (io->Pte(io->Ctx, buf / VSD_PAGE, pages, scratch) == 0) {
        return VSD_R_PTE;
    }
    for (i = 0; i < pages; i++) {
        if ((scratch[i] & VSD_PTE_PRESENT) == 0) {
            return VSD_R_PRESENT;
        }
    }
    for (i = 0; i < pages; i++) {
        phys = (scratch[i] & VSD_PTE_FRAME) | (i == 0 ? off : 0);
        chunk = VSD_PAGE - (i == 0 ? off : 0);
        if (chunk > left) {
            chunk = left;
        }
        if (n != 0 && runs[n - 1].Len <= 0xFFFFFFFFUL - runs[n - 1].Addr &&
            runs[n - 1].Addr + runs[n - 1].Len == phys) {
            runs[n - 1].Len += chunk;
        } else {
            if (n == VSD_ROOM_RUNS) {
                return VSD_R_ROOM;
            }
            runs[n].Addr = phys;
            runs[n].Len = chunk;
            n++;
        }
        left -= chunk;
    }
    *nruns = n;
    return VSD_OK;
}

int VsdBuild(const VSD_REQ *req, const VSD_IO *io, VSD_RUN *runs,
             VSD_U32 *scratch, VSD_U32 *nruns, VSD_U32 *total)
{
    VSD_U32 want;
    VSD_U32 nfrag = 1;
    VSD_U32 sum;
    VSD_U32 first;
    VSD_U32 addr;
    VSD_U32 count;
    VSD_U32 buf;
    VSD_U32 bytes;
    VSD_U32 i;
    int r;

    *nruns = 0;
    *total = 0;
    if (!VsdBytes(req->XferCount, req->Chars, req->Shift, &want)) {
        return VSD_R_COUNT;
    }
    if (req->Sg) {
        r = VsdWalk(req, io, &nfrag, &sum, &first);
        if (r != VSD_OK) {
            return r;
        }
    } else {
        if (want > req->MaxXfer) {
            return VSD_R_MAX;
        }
        sum = want;
        first = req->BufferPtr;
    }
    if (sum < want) {
        return VSD_R_COVER;
    }
    if (VsdWraps(first, want)) {
        return VSD_R_WRAP;
    }
    if (req->SrbValid && req->SrbLength != 0) {
        if (sum < req->SrbLength) {
            return VSD_R_SRB_COVER;
        }
        /* The SRB's DataBuffer is not read: at this layer it is not set
         * yet (0, or the last request's value in a reused SRB, seen on the
         * 98 SE emulator); SCSIPORT.PDR sets it below from the IOR, the
         * linear buffer or the first fragment, which is the list's first
         * byte by construction (Stor98's investigation, section 7). */
        if (VsdWraps(first, req->SrbLength)) {
            return VSD_R_WRAP;
        }
    }
    if (!req->Sg) {
        if (VsdWraps(first, sum)) {
            return VSD_R_WRAP;
        }
        r = VsdAddFrag(io, first, sum, runs, scratch, nruns);
        if (r != VSD_OK) {
            *nruns = 0;
            return r;
        }
        *total = sum;
        return VSD_OK;
    }
    addr = req->BufferPtr;
    for (i = 0; i < nfrag; i++) {
        io->Desc(io->Ctx, addr, &count, &buf);
        if (!VsdBytes(count, req->Chars, req->Shift, &bytes) || bytes == 0) {
            *nruns = 0;
            return VSD_R_COUNT;
        }
        r = VsdAddFrag(io, buf, bytes, runs, scratch, nruns);
        if (r != VSD_OK) {
            *nruns = 0;
            return r;
        }
        addr += 8;
    }
    *total = sum;
    return VSD_OK;
}

/* "stor98" in any letter case, then a NUL or the field's end. */
int VsdClaimTest(const unsigned char *port, VSD_U32 devflags, VSD_U32 dmd,
                 VSD_U32 maxxfer)
{
    const char *want = "stor98";
    unsigned i;
    unsigned char c;

    if (devflags & (VSD_DCB_DEV_LOGICAL | VSD_DCB_DEV_RMM)) {
        return VSD_C_LOGICAL;
    }
#ifdef VSD_TEST_NVME2K
    /* Emulator test builds only (never packaged): the read-first image
     * names Stor98's file NVME2K.MPD, so its port is nvme2k. */
    if ((port[0] | 0x20) == 'n' && (port[1] | 0x20) == 'v' &&
        (port[2] | 0x20) == 'm' && (port[3] | 0x20) == 'e' && port[4] == '2' &&
        (port[5] | 0x20) == 'k' && port[6] == 0) {
        want = "nvme2k";
    }
#endif
    for (i = 0; i < 6; i++) {
        c = port[i];
        if (c >= 'A' && c <= 'Z') {
            c = (unsigned char)(c - 'A' + 'a');
        }
        if (c != (unsigned char)want[i]) {
            return VSD_C_PORT;
        }
    }
    if (port[6] != 0) {
        return VSD_C_PORT;
    }
    if ((dmd & VSD_DMD_PHYS_SGD) == 0) {
        return VSD_C_NOPHYS;
    }
    if (maxxfer > VSD_MAX_XFER) {
        return VSD_C_MAX;
    }
    return VSD_CLAIM;
}

/* IOR.H's function codes that carry no data by their definition. */
int VsdNoDataFunc(unsigned func)
{
    switch (func) {
    case 0x03:  /* IOR_CANCEL */
    case 0x05:  /* IOR_MEDIA_CHECK */
    case 0x06:  /* IOR_MEDIA_CHECK_RESET */
    case 0x07:  /* IOR_LOAD_MEDIA */
    case 0x08:  /* IOR_EJECT_MEDIA */
    case 0x09:  /* IOR_LOCK_MEDIA */
    case 0x0A:  /* IOR_UNLOCK_MEDIA */
    case 0x10:  /* IOR_CLEAR_QUEUE */
    case 0x11:  /* IOR_DOS_RESET */
    case 0x23:  /* IOR_RESTART_QUEUE */
    case 0x24:  /* IOR_ABORT_QUEUE */
    case 0x25:  /* IOR_SPIN_DOWN */
    case 0x26:  /* IOR_SPIN_UP */
    case 0x27:  /* IOR_FLUSH_DRIVE */
    case 0x28:  /* IOR_FLUSH_DRIVE_AND_DISCARD */
        return 1;
    default:
        return 0;
    }
}
