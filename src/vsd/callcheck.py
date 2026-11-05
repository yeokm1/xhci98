# callcheck.py <vxd> <map> <obj>... | --self-test: every call the linker resolved must reach the function the
# compiler named. For each object, reads its code segment's self-relative fixups to external
# symbols and its public definitions there (Open Watcom's dmpobj), places the object's code in the
# image by its publics' map addresses, then checks that each such call's target in the linked
# image is the map address of the symbol the fixup names. Catches the 98 DDK's LINK 5.12
# resolving a reference to a different symbol (roadmap-usb4 task 43.46, 2026-10-22; design
# record 30 section 6.1). Open Watcom writes _TEXT as an object's first segment (index 1).
# Since 2026-11-03 it also holds every segment-relative (M=1) fixup of every object to the image:
# LINK 5.12 resolved each fixup to an object's own segment wrongly when the object's THEADR name
# was a multiple of 18 characters long (record 30 section 6.1). For each such fixup the image's LE
# fixup table must hold, at the place the map puts it, exactly one internal 32-bit offset to the
# code object at the address the object names (the map address of the external symbol, or the
# object's own segment placed by that segment's publics, plus the addend in the object's data and
# the fixup's displacement), and every LE fixup must be one of these. Any other fixup shape, or an
# image of more than one object, fails. --self-test runs both halves on synthetic input.
import re, subprocess, struct, sys
CODE = 1


class Refuse(Exception):
    pass


def le_fixups(d):
    # The LE fixup table as {offset in object 1: set of (source type, target object, target offset)}.
    le = struct.unpack_from('<I', d, 0x3c)[0]
    U = lambda o: struct.unpack_from('<I', d, le + o)[0]
    ps, npages, nobj, ot = U(0x28), U(0x14), U(0x44), le + U(0x40)
    fp, fr = le + U(0x68), le + U(0x6c)
    if nobj != 1:
        raise Refuse('the image has %d objects, not one' % nobj)
    vs, _, _, pti, npe, _ = struct.unpack_from('<6I', d, ot)
    if pti < 1 or pti - 1 + npe > npages:
        raise Refuse("the object's pages %d..%d lie outside the %d pages" % (pti, pti + npe - 1, npages))
    pt = [struct.unpack_from('<I', d, fp + 4 * i)[0] for i in range(npages + 1)]
    fix = {}; tails = []
    for pg in range(npages):
        o, e = fr + pt[pg], fr + pt[pg + 1]
        if o < e and not pti - 1 <= pg < pti - 1 + npe:
            raise Refuse('LE fixups on page %d, outside the object' % (pg + 1))
        while o < e:
            src, flg = d[o], d[o + 1]; o += 2
            if src & ~0x20 != 7 or flg & ~0x50:
                raise Refuse('an LE fixup of source %#x flags %#x on page %d, not an internal 32-bit offset' % (src, flg, pg + 1))
            srcs = []
            if src & 0x20:
                cnt = d[o]; o += 1
            else:
                srcs.append(struct.unpack_from('<h', d, o)[0]); o += 2
            tobj = d[o]; o += 1
            if flg & 0x40:
                tobj |= d[o] << 8; o += 1
            if flg & 0x10:
                tgt = struct.unpack_from('<I', d, o)[0]; o += 4
            else:
                tgt = struct.unpack_from('<H', d, o)[0]; o += 2
            if src & 0x20:
                srcs += [struct.unpack_from('<h', d, o + 2 * i)[0] for i in range(cnt)]; o += 2 * cnt
            for s in srcs:
                at = (pg - (pti - 1)) * ps + s
                if s < 0:
                    # the tail of a fixup across a page boundary, listed again on its second page
                    tails.append((at, (src & 0x0F, tobj, tgt), pg))
                    continue
                fix.setdefault(at, set()).add((src & 0x0F, tobj, tgt))
        if o != e:
            raise Refuse("page %d's LE fixups run past their end" % (pg + 1))
    for at, f, pg in tails:
        if at < -3 + (pg - (pti - 1)) * ps or f not in fix.get(at, set()):
            raise Refuse("a fixup across page %d's start at %#x that its first page lacks" % (pg + 1, at))
    return fix


def omf_fixups(path, b, pub):
    # Every M=1 fixup of an OMF object as (object, place in object 1, the LE fixup it needs, its
    # target's name), its segments placed by their publics' map addresses (pub: name -> offset in
    # object 1). Self-relative fixups to externals (calls) are the check above's.
    def idx(x, j):
        v = x[j]; j += 1
        if v & 0x80:
            v = ((v & 0x7F) << 8) | x[j]; j += 1
        return v, j
    i = 0; ext = [None]; base = {}; last = None; raw = []; pend = []
    while i + 3 <= len(b):
        t = b[i]; n = struct.unpack_from('<H', b, i + 1)[0]; x = b[i + 3:i + 2 + n]; i += 3 + n
        if t in (0x90, 0x91):
            j = 0; _, j = idx(x, j); sg, j = idx(x, j)
            if sg == 0:
                j += 2
            while j < len(x):
                l = x[j]; nm = x[j + 1:j + 1 + l].decode('latin-1'); j += 1 + l
                off = struct.unpack_from('<I' if t == 0x91 else '<H', x, j)[0]; j += 4 if t == 0x91 else 2
                _, j = idx(x, j)
                if nm in pub and sg:
                    if base.setdefault(sg, pub[nm] - off) != pub[nm] - off:
                        raise Refuse('%s: the publics of segment %d place it at %#x and %#x' % (path, sg, base[sg], pub[nm] - off))
        elif t == 0x8C:
            j = 0
            while j < len(x):
                l = x[j]; ext.append(x[j + 1:j + 1 + l].decode('latin-1')); j += 1 + l; _, j = idx(x, j)
        elif t in (0xB0, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xBC, 0xC2, 0xC3, 0xC4, 0xC5):
            raise Refuse('%s: a record %#x this check does not read' % (path, t))
        elif t in (0xA0, 0xA1):
            sg, j = idx(x, 0)
            off = struct.unpack_from('<I' if t == 0xA1 else '<H', x, j)[0]; j += 4 if t == 0xA1 else 2
            last = (sg, off, x[j:])
        elif t in (0xA2, 0xA3):
            last = None
        elif t in (0x9C, 0x9D):
            j = 0
            while j < len(x):
                c = x[j]
                if not c & 0x80:
                    raise Refuse('%s: a fixup thread' % path)
                loc = ((c << 8) | x[j + 1]) & 0x3FF; M = (c >> 6) & 1; lt = (c >> 2) & 15; j += 2
                fd = x[j]; j += 1
                if fd & 0x88:
                    raise Refuse('%s: a fixup through a thread' % path)
                fm, tm, P = (fd >> 4) & 7, fd & 3, (fd >> 2) & 1
                if fm < 3:
                    _, j = idx(x, j)
                tdat, j = idx(x, j)
                disp = 0
                if not P:
                    disp = struct.unpack_from('<i' if t == 0x9D else '<h', x, j)[0]; j += 4 if t == 0x9D else 2
                if last is None:
                    raise Refuse('%s: a fixup to iterated data or to no data' % path)
                if lt != 9 or fm != 1 or tm not in (0, 2):
                    raise Refuse('%s: a fixup of location %d, frame method %d, target method %d' % (path, lt, fm, tm))
                if not M:
                    if tm != 2:
                        raise Refuse('%s: a self-relative fixup to a segment' % path)
                    continue
                sg, off, data = last
                if loc + 4 > len(data):
                    raise Refuse('%s: a fixup past its data record' % path)
                pend.append((sg, off + loc, tm, tdat, struct.unpack_from('<I', data, loc)[0] + disp))
    # Placed once every PUBDEF is read (omfpub.py moves them all to the end).
    for sg, at, tm, tdat, add in pend:
        if sg not in base:
            raise Refuse('%s: a fixup in segment %d, which no public places' % (path, sg))
        if tm == 0:
            if tdat not in base:
                raise Refuse('%s: a fixup to segment %d, which no public places' % (path, tdat))
            tb, what = base[tdat], 'segment %d' % tdat
        else:
            if tdat >= len(ext) or ext[tdat] not in pub:
                raise Refuse('%s: a fixup to external %d, not in the map' % (path, tdat))
            tb, what = pub[ext[tdat]], ext[tdat]
        raw.append((path, base[sg] + at, (7, 1, (tb + add) & 0xFFFFFFFF), what))
    return raw


def match(raw, fix):
    # A message for every object fixup the LE table does not hold as named, and for every LE fixup
    # no object fixup accounts for.
    out = []; seen = set()
    for path, at, want, what in raw:
        got = fix.get(at, set())
        if at in seen or got != {want}:
            out.append('MISMATCH %s at %#x (to %s): compiler %#x, image %s' % (path, at, what, want[2],
                       ', '.join('%#x' % g[2] for g in sorted(got)) or 'no fixup'))
        seen.add(at)
    for at in sorted(set(fix) - seen):
        out.append("MISMATCH the image's LE fixup at %#x is no object's" % at)
    return out


def self_test():
    def rec(t, body):
        r = bytes([t]) + struct.pack('<H', len(body) + 1) + body
        return r + bytes([-sum(r) & 0xFF])
    def obj(fixup):
        return b''.join([rec(0x80, b'\x03t.c'), rec(0x96, b'\x00\x05_TEXT\x04CODE\x04FLAT'),
                         rec(0x99, b'\xa9\x10\x00\x00\x00\x02\x03\x01'), rec(0x9A, b'\x04'),
                         rec(0x8C, b'\x01X\x00'),
                         rec(0xA1, b'\x01\x00\x00\x00\x00' + struct.pack('<IIII', 8, 0, 0, 0)),
                         rec(0x9D, fixup), rec(0x91, b'\x00\x01\x01F\x00\x00\x00\x00\x00'), rec(0x8A, b'\x00')])
    # An M=1 OFFSET386 fixup at 0, frame GRPDEF 1, target SEGDEF 1, no displacement (the addend 8
    # in the data), and one at 4 to external 1 (X); the segment's public F at 100h in the map.
    fx = b'\xe4\x00\x14\x01\x01' + b'\xe4\x04\x16\x01\x01'
    pub = {'F': 0x100, 'X': 0x200}
    fails = 0
    raw = omf_fixups('t.obj', obj(fx), pub)
    if [r[1:3] for r in raw] != [(0x100, (7, 1, 0x108)), (0x104, (7, 1, 0x200))]:
        print('callcheck self-test: the object fixups read as %r' % raw); return 1
    good = {0x100: {(7, 1, 0x108)}, 0x104: {(7, 1, 0x200)}}
    for name, fix, want in (('matched', good, 0),
                            ('the mislink (segment offset less 1000h)', {0x100: {(7, 1, 0xFFFFF008)}, 0x104: {(7, 1, 0x200)}}, 1),
                            ('a fixup missing', {0x104: {(7, 1, 0x200)}}, 1),
                            ('a fixup the objects lack', {0x100: {(7, 1, 0x108)}, 0x104: {(7, 1, 0x200)}, 0x110: {(7, 1, 0)}}, 1),
                            ('two fixups at one place', {0x100: {(7, 1, 0x108), (7, 1, 0)}, 0x104: {(7, 1, 0x200)}}, 1)):
        got = len(match(raw, fix))
        if got != want:
            print('callcheck self-test: %s: %d mismatches, not %d' % (name, got, want)); fails += 1
    for name, fx2 in (('a fixup thread', b'\x00\x01' + fx), ('a self-relative fixup to a segment', b'\xa4\x00\x14\x01\x01'),
                      ('a 16-bit offset', b'\xc4\x00\x14\x01\x01'), ('a fixup past its data', b'\xe4\x0e\x14\x01\x01')):
        try:
            omf_fixups('t.obj', obj(fx2), pub)
            print('callcheck self-test: %s was read' % name); fails += 1
        except Refuse:
            pass
    print('callcheck self-test: %s' % ('ok' if not fails else '%d failing' % fails))
    return 1 if fails else 0


if sys.argv[1:] == ['--self-test']:
    sys.exit(self_test())
vxd, mp = sys.argv[1:3]
objs = sys.argv[3:]
d = open(vxd, 'rb').read(); le = struct.unpack_from('<I', d, 0x3c)[0]
U = lambda o: struct.unpack_from('<I', d, le + o)[0]
ps, dp, ot, om = U(0x28), U(0x80), le + U(0x40), le + U(0x48)
vs, _, fl, pti, npe, _ = struct.unpack_from('<6I', d, ot)
img = bytearray(vs + ps)
npages = U(0x14)
for k in range(npe):
    e = d[om + 4 * (pti + k - 1): om + 4 * (pti + k)]; pn = (e[0] << 16) | (e[1] << 8) | e[2]
    # only ordinary file pages: what the loader maps for another type is not these bytes
    need = U(0x2c) if pn == npages else ps   # a whole page, but the module's last
    if e[3] != 0 or pn < 1 or pn > npages or dp + (pn - 1) * ps + need > len(d):
        print('callcheck: code page %d: type %d, number %d: not a whole file page this check can read' % (k, e[3], pn)); sys.exit(1)
    if need > ps:
        print('callcheck: the last page size %d exceeds the page size %d' % (need, ps)); sys.exit(1)
    src = d[dp + (pn - 1) * ps: dp + (pn - 1) * ps + need]; img[k * ps:k * ps + len(src)] = src
mapped = min(vs, sum(ps for _ in range(npe)))
pub = {}
mt = open(mp, encoding='latin-1').read()
for m in re.finditer(r'(?m)^\s*0001:([0-9a-f]{8})\s+(\S+)\s+[0-9a-f]{8}\s+\S+\.obj', mt):
    pub[m.group(2)] = int(m.group(1), 16)
n = bad = 0
for obj in objs:
    res = subprocess.run(['dmpobj', obj], capture_output=True, text=True)
    if res.returncode != 0 or 'MODEND' not in res.stdout:
        print('callcheck: dmpobj could not read %s (exit %d)' % (obj, res.returncode)); sys.exit(1)
    lines = res.stdout.splitlines()
    base = None; seg = None
    for l in lines:
        m = re.match(r'\s+Group: \d+, Seg: (\d+)', l)
        if m:
            seg = int(m.group(1)); continue
        m = re.match(r'\s+([0-9a-f]{8})h - \'(\S+)\' Type', l)
        if m and seg == CODE and m.group(2) in pub:
            b = pub[m.group(2)] - int(m.group(1), 16)
            if base is not None and b != base:
                print('callcheck: %s: its publics place its code at %#x and %#x' % (obj, base, b)); sys.exit(1)
            base = b
    ext = {}; cur = None
    for l in lines:
        m = re.match(r'\s+(\d+) - \'(\S+)\' Type', l)
        if m: ext[int(m.group(1))] = m.group(2)
    for l in lines:
        m2 = re.search(r'Seg index:(\d+) offset:([0-9a-f]+)h', l)
        if m2: cur = (int(m2.group(1)), int(m2.group(2), 16))
        m = re.match(r'\s+([0-9a-f]{4})h Self\s+OFFSET386\s+Frame: \S+\s+Target: EI\((\d+)\)', l)
        if m and cur and cur[0] == CODE:
            if base is None:
                print('callcheck: %s: calls, but no public to place its code by' % obj); sys.exit(1)
            loc = base + cur[1] + int(m.group(1), 16); name = ext[int(m.group(2))]
            if loc + 4 > mapped:
                print('callcheck: %s: a call at %#x lies outside the mapped code' % (obj, loc)); sys.exit(1)
            tgt = (loc + 4 + struct.unpack_from('<i', img, loc)[0]) & 0xffffffff
            n += 1
            if tgt >= mapped:
                print('callcheck: %s: a call at %#x targets %#x, outside the mapped code' % (obj, loc, tgt)); sys.exit(1)
            if pub.get(name) != tgt:
                bad += 1
                print('MISMATCH %s at %#x: compiler %s (%s), image %#x' % (obj, loc, name, hex(pub[name]) if name in pub else '?', tgt))
print('callcheck: %d linker-resolved calls, %d mismatching' % (n, bad))
try:
    fix = le_fixups(d); raw = []
    for obj in objs:
        raw += omf_fixups(obj, open(obj, 'rb').read(), pub)
except Refuse as e:
    print('callcheck: %s' % e); sys.exit(1)
mis = match(raw, fix)
for m in mis:
    print(m)
print('callcheck: %d segment-relative fixups, %d LE fixups, %d mismatching' % (len(raw), len(fix), len(mis)))
sys.exit(1 if bad or not n or mis or not raw else 0)
