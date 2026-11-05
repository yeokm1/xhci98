# omfpub.py <obj>: the two rewrites of a Watcom OMF object before the 98 DDK's LINK 5.12 /VXD reads
# it (design record 30 section 6.1).
# 1. Move every PUBDEF record to just before its MODEND. Why: LINK 5.12 takes an EXTDEF that follows
#    the same object's PUBDEF of that name as a second definition (LNK2005), and Watcom writes its
#    PUBDEFs in batches as it goes, so a call to a function whose batch was already written becomes
#    such an EXTDEF (omfcheck.py). A PUBDEF may sit anywhere after the SEGDEF/GRPDEF it names.
# 2. Set the THEADR name to the source file's base name (pfw.c), not the full path Watcom writes.
#    Why: LINK 5.12 resolves every fixup to the object's own segments wrongly when the THEADR name's
#    length is a multiple of 18 (2026-11-03), so the image hung on the checkout's path; with the
#    base name (and wcc386 -zld) the object is the same bytes wherever it is compiled. omfcheck.py
#    refuses a name still holding a path or of such a length.
# Each record keeps or gets its own checksum, so the rewritten object stays valid.
import ntpath, struct, sys
p = sys.argv[1]; d = open(p, 'rb').read(); i = 0; recs = []
while i + 3 <= len(d):
    t = d[i]; n = struct.unpack_from('<H', d, i + 1)[0]; recs.append((t, d[i:i + 3 + n])); i += 3 + n
assert i == len(d) and recs[-1][0] in (0x8A, 0x8B), 'not a whole OMF object'
assert recs[0][0] == 0x80 and recs[0][1][3] + 5 == len(recs[0][1]), 'no THEADR record first'
name = ntpath.basename(recs[0][1][4:-1].decode('latin-1')).encode('latin-1')
assert 0 < len(name) < 256, 'no file name in the THEADR record'
body = bytes([0x80]) + struct.pack('<H', len(name) + 2) + bytes([len(name)]) + name
recs[0] = (0x80, body + bytes([-sum(body) & 0xFF]))
pub = [r for t, r in recs if t in (0x90, 0x91)]
rest = [r for t, r in recs[:-1] if t not in (0x90, 0x91)]
open(p, 'wb').write(b''.join(rest + pub + [recs[-1][1]]))
print('omfpub: moved %d PUBDEF records, THEADR %s' % (len(pub), name.decode('latin-1')))
