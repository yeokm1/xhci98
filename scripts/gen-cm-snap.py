# gen-cm-snap.py - the field tables of xhcicm.sys's state snapshot.
#
# Design record 21 section 8.4 and design record 25 section 23 (roadmap-usb4
# task 49.37). Reads the connection manager's structures from src\cm\*.h and
# writes the field directories the driver's snapshot gathers by:
#   src\cm\cm_snapf.c  the pure core's structures (host-tested)
#   src\cm\cm_snapn.h  CM_NHI and the kernel-side structures (cm_snapos.c)
# Run it again whenever one of those structures changes; the gated build's
# check (-Check) fails when the files it would write differ from the tree's.
#
# What is listed, and why it is safe to serve to any user at any level:
# only integer fields (ULONG, LONG, USHORT, UCHAR, BOOLEAN and the enums);
# never a pointer, a function pointer, a kernel object, a physical, bus or
# virtual address, a devnode handle, a character string or a property
# block; identities (UUIDs, UIDs, MAC addresses, the ICM's network serial)
# only as a 16-bit hash under a salt that changes at every start; the ICM's
# approval key never. Usage: gen-cm-snap.py <repo> [-Check]

import os
import re
import sys

ROOT = sys.argv[1]
CHECK = len(sys.argv) > 2 and sys.argv[2] == '-Check'
CM = os.path.join(ROOT, 'src', 'cm')
HEADERS = [os.path.join(CM, f) for f in sorted(os.listdir(CM)) if f.endswith('.h')]
HEADERS += [os.path.join(ROOT, 'src', 'xhci_log.h'),
            os.path.join(ROOT, 'src', 'xhci_cmnet.h')]

SCALAR = {
    'ULONG': (4, 0), 'LONG': (4, 1), 'NTSTATUS': (4, 1), 'USHORT': (2, 0),
    'UCHAR': (1, 0), 'BOOLEAN': (1, 0), 'KIRQL': (1, 0),
    'DEVICE_POWER_STATE': (4, 0), 'SYSTEM_POWER_STATE': (4, 0),
    'KINTERRUPT_MODE': (4, 0),
}
HASH_TYPES = {'TBT_UUID'}
HASH_NAMES = re.compile(r'^(Uuid|ChildUuid|ParentUuid|KidUuid|Mac|PeerMac|'
                        r'NetMac|XdMac|NetSerialLo|NetSerialHi)$')
# Addresses, handles, secrets and blocks that can hold names.
EXCLUDE_NAMES = re.compile(r'^(Key|Phys|\w*Pa|\w*Va|Devnode|LiveBar0|'
                           r'LiveBar0Hi|\w*Block|RemoteInfo|LocalProps|'
                           r'Affinity|Ecam\w*|Mcfg\w*|BrDiag|NiDiag|BqDiag)$')
# Types never listed: kernel objects, addresses, strings, interfaces, and
# a held packet (TBT_FRAME: its words can carry the UUIDs of an XDomain
# request and a peer's property block). A
# member of any other type the rules do not place stops the generator, so a
# new member is either listed or left out on purpose, never by accident.
OPAQUE = {'KSPIN_LOCK', 'KEVENT', 'KDPC', 'KTIMER', 'LIST_ENTRY',
          'PHYSICAL_ADDRESS', 'LARGE_INTEGER', 'KAFFINITY',
          'BUS_INTERFACE_STANDARD', 'char', 'WCHAR', 'TBT_FRAME'}
UNPLACED = []
POINTERS = {'PVOID', 'PIRP', 'PDEVICE_OBJECT', 'PKINTERRUPT', 'PDMA_ADAPTER',
            'PKSPIN_LOCK', 'PUCHAR', 'PULONG', 'PCHAR', 'PXHCI_LOG'}
# Members of CM_NHI served elsewhere, as tables of their own or not at all.
NHI_SKIP = {'Common', 'Log', 'LogHead', 'LogEmit', 'Host', 'Cfg', 'Ring', 'Icm',
            'U4', 'Reenum', 'Counters', 'Rirq', 'Net', 'U4Batch', 'QueryId',
            'QueryExt', 'QueryBuf', 'QueryLast', 'MarksBuf', 'NetQBuf',
            'QueryCtr', 'StatusPub', 'Ej', 'U4Carry', 'ChainPushed'}
MAX_ARRAY = 64          # elements; a larger array is a buffer, not state

# The tables: name on the wire, structure, own tables it leaves out, file.
PURE = [
    ('TBT_U4CM', {'Ports', 'Links', 'Kids', 'Obligations', 'Counters', 'Xd',
                  'Ctl', 'Net', 'Carried', 'ResumeSaved', 'SnapPorts',
                  'SnapHost', 'SnapLinks', 'Scratch', 'Config'}),
    ('TBT_U4_PORT', set()), ('TBT_U4_LINK', set()), ('TBT_U4_KID', set()),
    ('TBT_U4_OBLIGATION', set()), ('TBT_U4_COUNTERS', set()),
    ('TBT_U4_XD', set()),
    ('TBT_ICM', {'Devices', 'Counters', 'Xd', 'XdCounters', 'Net'}),
    ('TBT_ICM_DEVICE', set()), ('TBT_ICM_COUNTERS', set()),
    ('TBT_RING', set()), ('XHCI_CHAIN_BIND', set()),
]
KERNEL = [
    ('CM_NHI', NHI_SKIP), ('CM_COUNTERS', set()), ('CM_RIRQ_DRV', set()),
    ('CM_NET', set()), ('CM_REENUM', {'Port', 'Marks', 'RecTopo'}),
    ('CM_REENUM_PORT', set()),
]


def load():
    structs = {}
    for f in HEADERS:
        t = open(f, encoding='latin-1').read()
        t = re.sub(r'/\*.*?\*/', ' ', t, flags=re.S)
        for m in re.finditer(r'typedef struct _(\w+) \{', t):
            i = m.end()
            depth = 1
            while depth:
                if t[i] == '{':
                    depth += 1
                elif t[i] == '}':
                    depth -= 1
                i += 1
            structs[m.group(1)] = t[m.end():i - 1]
    return structs


def members(body):
    out = []
    cur = ''
    depth = 0
    pp = 0
    for line in body.split('\n'):
        s = line.strip()
        if s.startswith('#'):
            if re.match(r'#\s*if', s):
                pp += 1
            elif re.match(r'#\s*endif', s):
                pp -= 1
            continue
        if pp:
            continue
        for ch in line + ' ':
            if ch == '{':
                depth += 1
            if ch == '}':
                depth -= 1
            cur += ch
            if ch == ';' and depth == 0:
                out.append(' '.join(cur.split()))
                cur = ''
    return out


def fields(structs, ctype, skip, prefix='', body=None):
    """(name, C member expression, width, flags) for every field listed."""
    out = []
    for m in members(body if body is not None else structs[ctype]):
        m = m.rstrip(';').strip()
        anon = re.match(r'^struct \{(.*)\} (\w+)$', m, re.S)
        if anon:
            if anon.group(2) not in skip:
                out += fields(structs, ctype, set(), prefix + anon.group(2) + '.',
                              anon.group(1))
            continue
        outside = re.sub(r'\[[^\]]*\]', '', m)
        if '(' in outside or '*' in outside:
            continue                    # a pointer or a function pointer
        m = re.sub(r'\b(volatile|const)\b', '', m).strip()
        mm = re.match(r'^(\w+) (\w+)((?:\[[^\]]+\])*)$', m)
        if not mm:
            UNPLACED.append('%s: %s' % (ctype, m))
            continue
        typ, name, dims = mm.groups()
        if typ in OPAQUE or typ in FNTYPES:
            continue
        if typ in POINTERS or (typ.startswith('P') and typ[1:] in structs):
            continue                    # a pointer typedef
        if not prefix and name in skip:
            continue
        if EXCLUDE_NAMES.match(name):
            continue
        expr = prefix + name
        if typ in HASH_TYPES or HASH_NAMES.match(name):
            out.append((expr, expr, 0, 'CM_QS_FD_HASH'))
            continue
        if typ in SCALAR:
            width, signed = SCALAR[typ]
            n = 1
            for d in re.findall(r'\[([^\]]+)\]', dims):
                v = dim_value(d)
                if v is None:
                    UNPLACED.append('%s: %s (its bound is not known)' % (ctype, m))
                    n = 0
                    break
                n *= v
            if n == 0 or n > MAX_ARRAY:
                continue
            out.append((expr, expr, width, 'CM_QS_FD_SIGNED' if signed else '0'))
            continue
        if typ in structs:
            if not dims:
                out += fields(structs, typ, set(), expr + '.')
            continue
        UNPLACED.append('%s: %s' % (ctype, m))
    return out


DEFINES = {}


def dim_value(d):
    e = d.strip()
    for k in sorted(DEFINES, key=len, reverse=True):
        e = re.sub(r'\b%s\b' % k, str(DEFINES[k]), e)
    e = re.sub(r'\b(\d+)UL?\b', r'\1', e)
    if re.match(r'^[\d\s\+\*\-\(\)/]+$', e):
        return int(eval(e.replace('/', '//')))
    return None


def load_defines():
    for f in HEADERS:
        for line in open(f, encoding='latin-1'):
            m = re.match(r'#define (\w+)\s+\(?(\d+)(?:UL?)?\)?\s*(/\*.*)?$', line.strip())
            if m:
                DEFINES[m.group(1)] = int(m.group(2))
    # A few bounds are written as expressions.
    changed = True
    while changed:
        changed = False
        for f in HEADERS:
            for line in open(f, encoding='latin-1'):
                m = re.match(r'#define (\w+)\s+\((.*)\)\s*(/\*.*)?$', line.strip())
                if m and m.group(1) not in DEFINES:
                    e = m.group(2)
                    for k, v in DEFINES.items():
                        e = re.sub(r'\b%s\b' % k, str(v), e)
                    e = re.sub(r'(\d+)UL?', r'\1', e)
                    if re.match(r'^[\d\s\+\*\-\(\)/]+$', e):
                        DEFINES[m.group(1)] = int(eval(e.replace('/', '//')))
                        changed = True


HEAD = ('/*\r\n * %s - generated by scripts\\gen-cm-snap.py from the structures in\r\n'
        ' * src\\cm\\*.h: the field directories of xhcicm.sys\'s state snapshot\r\n'
        ' * (design record 21 section 8.4). Do not edit; run the script again.\r\n'
        ' * %s\r\n */\r\n\r\n')


def emit(structs, tables, fname, note, includes):
    lines = [HEAD % (fname, note)]
    for inc in includes:
        lines.append('#include "%s"\r\n' % inc)
    lines.append('\r\n')
    longest = 0
    for ctype, skip in tables:
        fl = fields(structs, ctype, skip)
        lines.append('const CM_QS_FIELD CmQsF_%s[] = {\r\n' % ctype)
        for name, expr, width, flags in fl:
            longest = max(longest, len(name))
            lines.append('    CM_QS_F(%s, %s, %d, %s),\r\n' % (ctype, expr, width, flags))
        lines.append('};\r\nconst ULONG CmQsN_%s = sizeof(CmQsF_%s) / sizeof(CmQsF_%s[0]);\r\n\r\n'
                     % (ctype, ctype, ctype))
    return ''.join(lines), longest


FNTYPES = set()


def load_fntypes():
    for f in HEADERS:
        t = open(f, encoding='latin-1').read()
        for m in re.finditer(r'typedef\s+[\w\s\*]+?\(\s*(?:\w+\s+)?\*\s*(\w+)\s*\)', t):
            FNTYPES.add(m.group(1))


def main():
    structs = load()
    load_fntypes()
    load_defines()
    pure, l1 = emit(structs, PURE, 'cm_snapf.c',
                    'The pure core\'s structures, host-tested (test_cm_snap).',
                    ['cm_snapb.h', 'tbt_u4cm.h', 'tbt_icm.h', 'tbt_ring.h',
                     'tbt_chain.h'])
    kern, l2 = emit(structs, KERNEL, 'cm_snapn.h',
                    'CM_NHI and the kernel-side structures; cm_snapos.c alone.', [])
    if UNPLACED:
        sys.exit('gen-cm-snap: members no rule places (list them, or add '
                 'their type to OPAQUE):\n  ' + '\n  '.join(UNPLACED))
    if max(l1, l2) > 47:
        sys.exit('a field name of %d characters: CM_QS_NAME_MAX is 47' % max(l1, l2))
    rc = 0
    for name, text in (('cm_snapf.c', pure), ('cm_snapn.h', kern)):
        path = os.path.join(CM, name)
        data = text.encode('ascii')
        if CHECK:
            old = open(path, 'rb').read() if os.path.exists(path) else b''
            if old != data:
                print('gen-cm-snap: %s is not what the structures give; run '
                      'scripts\\gen-cm-snap.py' % name)
                rc = 1
        else:
            open(path, 'wb').write(data)
    if CHECK and rc == 0:
        print('gen-cm-snap: the snapshot\'s field tables agree with the structures')
    sys.exit(rc)


main()
