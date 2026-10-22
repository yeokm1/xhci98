# omfpub.py <obj>: move every PUBDEF record of a Watcom OMF object to just before its MODEND.
# Why: the 98 DDK's LINK 5.12 takes an EXTDEF that follows the same object's PUBDEF of that name as
# a second definition (LNK2005), and Watcom writes its PUBDEFs in batches as it goes, so a call to
# a function whose batch was already written becomes such an EXTDEF (omfcheck.py). A PUBDEF may sit
# anywhere after the SEGDEF/GRPDEF it names; each record keeps its own checksum, so moving whole
# records leaves them valid.
import struct, sys
p = sys.argv[1]; d = open(p, 'rb').read(); i = 0; recs = []
while i + 3 <= len(d):
    t = d[i]; n = struct.unpack_from('<H', d, i + 1)[0]; recs.append((t, d[i:i + 3 + n])); i += 3 + n
assert i == len(d) and recs[-1][0] in (0x8A, 0x8B), 'not a whole OMF object'
pub = [r for t, r in recs if t in (0x90, 0x91)]
rest = [r for t, r in recs[:-1] if t not in (0x90, 0x91)]
open(p, 'wb').write(b''.join(rest + pub + [recs[-1][1]]))
print('omfpub: moved %d PUBDEF records' % len(pub))
