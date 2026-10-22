# callcheck.py <vxd> <map> <obj>...: every call the linker resolved must reach the function the
# compiler named. For each object, reads its code segment's self-relative fixups to external
# symbols and its public definitions there (Open Watcom's dmpobj), places the object's code in the
# image by its publics' map addresses, then checks that each such call's target in the linked
# image is the map address of the symbol the fixup names. Catches the 98 DDK's LINK 5.12
# resolving a reference to a different symbol (roadmap-usb4 task 43.46, 2026-10-22; design
# record 30 section 6.1). Open Watcom writes _TEXT as an object's first segment (index 1).
import re, subprocess, struct, sys
CODE = 1
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
print('callcheck: %d linker-resolved calls, %d mismatching' % (n, bad)); sys.exit(1 if bad or not n else 0)
