/*
 * vsd.c - S98VSD.VXD, the Windows 98 SE and ME IOS vendor-supplied driver
 * that lets large requests reach Stor98 whole (roadmap-usb4.md task 43.46,
 * design record 30).
 *
 * On a physical DCB whose port is stor98 it inserts a calldown entry above
 * SCSIPORT.PDR that demands everything SCSIPORT.PDR demanded but
 * DCB_dmd_phys_sgd, so IOS's criteria routine no longer cuts a request at
 * 512 KiB or 17 runs; in exchange it builds the physical list SCSIPORT.PDR
 * walks at IOR_sgd_lin_phys for every request that carries data (section
 * 4), and puts the IOR's fields back in a callback.
 *
 * Context: IOS calls the AER and the request routine at its own times
 * (the request routine possibly at interrupt time, as every IOS layer's);
 * nothing here waits, allocates or takes a lock. No file-scope static
 * anywhere (the 98 DDK's LINK 5.12 mislinks Watcom's static-symbol
 * records, section 6.1); the data lives in vsd_data.c.
 */

#include "vsd.h"
#ifdef VSD_DEBUGCON
#include <conio.h>
#endif

/* VMM's _CopyPageTable and IOS's IOS_Register, both C-callable (cdecl):
 * the int 20h call with the service's dword (vsd_vmm.h), which VMM
 * patches into a direct call on first use. C cannot emit a dynalink, so
 * these two are byte sequences; the parameters are ordinary C ones. */
ULONG VsdCopyPageTable(ULONG page, ULONG npages, ULONG *ptes, ULONG flags);
#pragma aux VsdCopyPageTable = 0xcd 0x20 0x61 0x00 0x01 0x00 \
        parm caller [] value [eax] modify [eax ecx edx];

void VsdIosRegister(DRP *drp);
#pragma aux VsdIosRegister = 0xcd 0x20 0x07 0x00 0x10 0x00 \
        parm caller [] modify [eax ecx edx];

#ifdef VSD_DEBUGCON
/* The qemu build's text to QEMU's debug console (section 4.9). */
void VsdPut(const char *s)
{
    while (*s) {
        outp(0xE9, *s++);
    }
}

void VsdHex(ULONG v)
{
    const char *d = "0123456789abcdef";
    int i;

    for (i = 28; i >= 0; i -= 4) {
        outp(0xE9, d[(v >> i) & 0xF]);
    }
}

void VsdLine(const char *what, ULONG a, ULONG b)
{
    VsdPut("s98vsd ");
    VsdPut(what);
    VsdPut(" ");
    VsdHex(a);
    VsdPut(" ");
    VsdHex(b);
    VsdPut("\r\n");
}

void VsdChain(DCB *dcb)
{
    DCB_cd_entry *cd;

    for (cd = (DCB_cd_entry *)dcb->DCB_cmn.DCB_ptr_cd; cd != NULL;
         cd = (DCB_cd_entry *)cd->DCB_cd_next) {
        VsdLine("chain lgn/flags", cd->DCB_cd_lgn, cd->DCB_cd_flags);
    }
}
#define VSD_LINE(w, a, b)   VsdLine((w), (ULONG)(a), (ULONG)(b))
#define VSD_CHAIN(d)        VsdChain(d)
#else
#define VSD_LINE(w, a, b)   ((void)0)
#define VSD_CHAIN(d)        ((void)0)
#endif

/* ---- the AER (section 3.3) --------------------------------------------- */

/* 1 when the DCB's chain already holds this layer's entry. */
int VsdClaimed(DCB *dcb)
{
    DCB_cd_entry *cd;

    for (cd = (DCB_cd_entry *)dcb->DCB_cmn.DCB_ptr_cd; cd != NULL;
         cd = (DCB_cd_entry *)cd->DCB_cd_next) {
        if (cd->DCB_cd_io_address == (PVOID)VsdRequest) {
            return 1;
        }
    }
    return 0;
}

void VsdConfig(AEP_dcb_config *aep, DCB *dcb)
{
    ISP_calldown_insert isp;
    int why;

    why = VsdClaimTest(dcb->DCB_port_name, dcb->DCB_cmn.DCB_device_flags,
                       dcb->DCB_cmn.DCB_dmd_flags, dcb->DCB_max_xfer_len);
    if (why != VSD_CLAIM) {
        VsdCounts.Left[why]++;
        VSD_LINE("left dcb/why", dcb, why);
        VSD_LINE("left port", *(ULONG *)&dcb->DCB_port_name[0],
                 *(ULONG *)&dcb->DCB_port_name[4]);
        return;
    }
    if (VsdClaimed(dcb)) {
        return;
    }
    isp.ISP_i_cd_hdr.ISP_func = ISP_INSERT_CALLDOWN;
    isp.ISP_i_cd_hdr.ISP_result = 0;
    isp.ISP_i_cd_flags = dcb->DCB_cmn.DCB_dmd_flags & ~DCB_dmd_phys_sgd;
    isp.ISP_i_cd_dcb = (ULONG)dcb;
    isp.ISP_i_cd_req = (PVOID)VsdRequest;
    isp.ISP_i_cd_lgn = aep->AEP_d_c_hdr.AEP_lgn;
    isp.ISP_i_cd_ddb = 0;
    isp.ISP_i_cd_expan_len = sizeof(VSD_AREA);
    ((void (__cdecl *)(ISP *))VsdIlb.ILB_service_rtn)(&isp.ISP_i_cd_hdr);
    if (isp.ISP_i_cd_hdr.ISP_result != 0) {
        VsdCounts.InsertFailed++;
        VSD_LINE("insert failed", dcb, isp.ISP_i_cd_hdr.ISP_result);
        return;
    }
    VsdCounts.Claimed++;
    VSD_LINE("claimed dcb/dmd", dcb, dcb->DCB_cmn.DCB_dmd_flags);
    VSD_CHAIN(dcb);
    /* Another layer's earlier entry may have kept phys_sgd; then IOS builds
     * every list and this layer passes each request down (section 3.4). */
    if (dcb->DCB_cmn.DCB_dmd_flags & DCB_dmd_phys_sgd) {
        VsdCounts.PhysLeft++;
        VSD_LINE("phys_sgd still demanded", dcb, dcb->DCB_cmn.DCB_dmd_flags);
    }
}

void __cdecl VsdAer(AEP *aep)
{
    aep->AEP_result = AEP_SUCCESS;
    if (aep->AEP_func == AEP_CONFIG_DCB) {
        VsdConfig((AEP_dcb_config *)aep,
                  (DCB *)((AEP_dcb_config *)aep)->AEP_d_c_dcb);
    }
}

/* ---- the request path (section 4) --------------------------------------- */

void VsdCallDown(IOP *iop)
{
    DCB_cd_entry *next;

    next = (DCB_cd_entry *)((DCB_cd_entry *)iop->IOP_calldown_ptr)->DCB_cd_next;
    iop->IOP_calldown_ptr = (ULONG)next;
    ((void (__cdecl *)(IOP *))next->DCB_cd_io_address)(iop);
}

/* The callback above this layer: the entry IOP_callback_ptr points below. */
void VsdCallBack(IOP *iop)
{
    IOP_callback_entry *cb;

    cb = (IOP_callback_entry *)iop->IOP_callback_ptr - 1;
    iop->IOP_callback_ptr = (ULONG)cb;
    ((void (__cdecl *)(IOP *))cb->IOP_CB_address)(iop);
}

/* Complete a request this layer will not send down (section 4.5); none of
 * the IOR's fields has been changed yet. */
void VsdRefuse(IOP *iop, ULONG *counter)
{
    (*counter)++;
    iop->IOP_ior.IOR_status = IORS_INVALID_COMMAND;
    VSD_LINE("refused func/flags", iop->IOP_ior.IOR_func, iop->IOP_ior.IOR_flags);
    VsdCallBack(iop);
}

/* The page-table read for vsd_list.c: one call for a fragment's pages. */
VSD_U32 VsdPte(void *ctx, VSD_U32 page, VSD_U32 npages, VSD_U32 *ptes)
{
    (void)ctx;
    return VsdCopyPageTable(page, npages, ptes, 0);
}

/* A logical list's descriptor (BLOCKDEV.H's _BlockDev_Scatter_Gather). */
void VsdDesc(void *ctx, VSD_U32 addr, VSD_U32 *count, VSD_U32 *buffer)
{
    _BlockDev_Scatter_Gather *sg = (_BlockDev_Scatter_Gather *)addr;

    (void)ctx;
    *count = sg->BD_SG_Count;
    *buffer = sg->BD_SG_Buffer_Ptr;
}

void __cdecl VsdRequest(IOP *iop)
{
    DCB *dcb = (DCB *)iop->IOP_physical_dcb;
    IOR *ior = &iop->IOP_ior;
    VSD_AREA *a;
    IOP_callback_entry *cb;
    VSD_REQ rq;
    VSD_IO io;
    UCHAR *srb = 0;
    ULONG srbLen = 0;
    VSD_U32 n;
    VSD_U32 total;
    int r;

    if (ior->IOR_flags & IORF_PHYS_SGDS) {
        VsdCounts.Prebuilt++;
        VsdCallDown(iop);
        return;
    }
    if (ior->IOR_flags & IORF_SRB_VALID) {
        srb = (UCHAR *)iop->IOP_srb;
        srbLen = *(ULONG *)(srb + VSD_SRB_DATA_LENGTH);
    }
    if (ior->IOR_xfer_count == 0 || VsdNoDataFunc(ior->IOR_func)) {
        if (srbLen != 0) {
            VSD_LINE("refusal: SRB data, no list; srb len", srbLen, ior->IOR_xfer_count);
            VsdRefuse(iop, &VsdCounts.SrbNoData);
            return;
        }
        VsdCounts.NoData++;
        VsdCallDown(iop);
        return;
    }
    /* No SRB construction for an incoming request without one is read
     * (section 4.3), so none is admitted. */
    if (srb == 0) {
        VSD_LINE("refusal: no SRB; count", ior->IOR_xfer_count, 0);
        VsdRefuse(iop, &VsdCounts.NoSrb);
        return;
    }
    cb = (IOP_callback_entry *)iop->IOP_callback_ptr;
    if (cb >= &iop->IOP_callback_table[IOP_CALLBACK_TABLE_DEPTH]) {
        VSD_LINE("refusal: callback table full", cb, iop);
        VsdRefuse(iop, &VsdCounts.CbFull);
        return;
    }
    a = (VSD_AREA *)((char *)iop +
        ((DCB_cd_entry *)iop->IOP_calldown_ptr)->DCB_cd_expan_off);
    rq.BufferPtr = ior->IOR_buffer_ptr;
    rq.XferCount = ior->IOR_xfer_count;
    rq.Sg = (ior->IOR_flags & IORF_SCATTER_GATHER) != 0;
    rq.Chars = (ior->IOR_flags & IORF_CHAR_COMMAND) != 0;
    rq.Shift = dcb->DCB_cmn.DCB_apparent_blk_shift;
    rq.MaxXfer = dcb->DCB_max_xfer_len < VSD_MAX_XFER ?
                 dcb->DCB_max_xfer_len : VSD_MAX_XFER;
    rq.SrbValid = 1;
    rq.SrbBuffer = *(ULONG *)(srb + VSD_SRB_DATA_BUFFER);
    rq.SrbLength = srbLen;
    io.Pte = VsdPte;
    io.Desc = VsdDesc;
    io.Ctx = 0;
    r = VsdBuild(&rq, &io, a->List, a->Scratch, &n, &total);
    if (r != VSD_OK) {
        VSD_LINE("refusal: build r/count", r, ior->IOR_xfer_count);
        VSD_LINE("refusal: buf/srbbuf", rq.BufferPtr, rq.SrbBuffer);
        VSD_LINE("refusal: srblen/flags", rq.SrbLength, ior->IOR_flags);
        VsdRefuse(iop, &VsdCounts.Refused[r]);
        return;
    }
    a->SaveSgd = ior->IOR_sgd_lin_phys;
    a->SaveNum = ior->IOR_num_sgds;
    a->SaveFlags = ior->IOR_flags;
    ior->IOR_sgd_lin_phys = (ULONG)a->List;
    /* A byte field; SCSIPORT.PDR walks the list by length (section 4.2). */
    ior->IOR_num_sgds = (UCHAR)(n > 255 ? 255 : n);
    ior->IOR_flags |= IORF_PHYS_SGDS;
    cb->IOP_CB_address = (ULONG)VsdCallback;
    cb->IOP_CB_ref_data = (ULONG)a;
    iop->IOP_callback_ptr = (ULONG)(cb + 1);
    VsdCounts.Built++;
    if (n > VsdCounts.MaxRuns) {
        VsdCounts.MaxRuns = n;
    }
    if (total > VsdCounts.MaxBytes) {
        VsdCounts.MaxBytes = total;
    }
#ifdef VSD_DEBUGCON
    if ((VsdCounts.Built & 0xFFF) == 1) {
        VSD_LINE("built/maxruns", VsdCounts.Built, VsdCounts.MaxRuns);
    }
#endif
    VsdCallDown(iop);
}

/* The completer has stepped IOP_callback_ptr back onto this layer's entry. */
void __cdecl VsdCallback(IOP *iop)
{
    VSD_AREA *a;
    IOR *ior = &iop->IOP_ior;

    a = (VSD_AREA *)((IOP_callback_entry *)iop->IOP_callback_ptr)->IOP_CB_ref_data;
    ior->IOR_sgd_lin_phys = a->SaveSgd;
    ior->IOR_num_sgds = a->SaveNum;
    ior->IOR_flags = (ior->IOR_flags & ~IORF_PHYS_SGDS) |
                     (a->SaveFlags & IORF_PHYS_SGDS);
    VsdCallBack(iop);
}

/* ---- the control procedure (section 3.6) -------------------------------- */

int VsdInit(void)
{
    VsdCounts.Stamp = (ULONG)VsdStamp;
    VsdIosRegister(&VsdDrp);
    VSD_LINE("registered", VsdDrp.DRP_reg_result, VsdCounts.Stamp);
    if (VsdDrp.DRP_reg_result == DRP_REMAIN_RESIDENT ||
        VsdDrp.DRP_reg_result == DRP_MINIMIZE) {
        VsdCounts.Registered = 1;
        return 0;
    }
    return 1;
}

/* VMM passes the message in EAX; the C result 0 is success. */
int VsdCtrl(ULONG msg);
#pragma aux VsdCtrl parm [eax] value [eax];
int VsdCtrl(ULONG msg)
{
    if (msg == VSD_SYS_DYNAMIC_INIT) {
        return VsdInit();
    }
    /* IOS holds the AER, the request routine and the callbacks of a
     * registered layer and has no call that takes them back: never be
     * unloaded once registered (section 3.6). */
    if (msg == VSD_SYS_DYNAMIC_EXIT && VsdCounts.Registered) {
        return 1;
    }
    return 0;
}

void __declspec(naked) VsdControl(void)
{
    __asm {
        call VsdCtrl    ; the C handler: VMM's message in EAX, the result back in EAX
        neg  eax        ; carry = (EAX != 0), VMM's failure flag: C cannot set a flag
        ret             ; a naked function gets no epilogue from the compiler
    }
}
