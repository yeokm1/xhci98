# omfcheck.py <obj>...: fail on the Watcom OMF shapes that the 98 DDK's LINK 5.12 /VXD mislinks.
# 43.46 finding: LINK 5.12 handles an EXTDEF (or static LEXTDEF) that names a symbol the same module
# already defined in an earlier PUBDEF/LPUBDEF wrongly - a public one becomes "already defined"
# (LNK2005), a static one is resolved to a DIFFERENT symbol (a call to a static function landed on
# another one, so the VxD crashed before its first line ran). A reference made before the
# definition (EXTDEF first, PUBDEF later) links correctly. So: no static symbol records at all,
# and no EXTDEF after its PUBDEF.
# 2026-11-03 finding: LINK 5.12 resolves every fixup to the object's own segments (a SEGDEF target,
# such as -zc string literals) wrongly when the THEADR name's length is a multiple of 18 (design
# record 30 section 6.1). omfpub.py sets the name to the source's base name; so: a THEADR name with
# no path in it, and of no such length (a source file of 18 characters is to be renamed).
import struct, sys
bad = []
for fn in sys.argv[1:]:
    d = open(fn, 'rb').read(); i = 0; defined = set()
    while i + 3 <= len(d):
        t = d[i]; n = struct.unpack_from('<H', d, i + 1)[0]; body = d[i + 3:i + 2 + n]
        if t in (0x80, 0x82):            # THEADR / LHEADR: one name
            name = body[1:1 + body[0]].decode('latin-1') if body else ''
            if i != 0 or t != 0x80 or not body or body[0] + 1 != len(body):
                bad.append('%s: a header record %#x at %#x that is not the one THEADR first' % (fn, t, i))
            elif not name or any(c in name for c in '\\/:'):
                bad.append('%s: THEADR name %r is not a file name alone (omfpub.py not run?)' % (fn, name))
            elif len(name) % 18 == 0:
                bad.append('%s: THEADR name %r is %d characters long, a multiple of 18' % (fn, name, len(name)))
        if t in (0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xBA, 0xBB):
            bad.append('%s: static-symbol record %#x at %#x' % (fn, t, i))
        if t in (0x90, 0x91):            # PUBDEF: group idx, seg idx, [frame], (name, off, type)*
            j = 0
            def idx():
                global j
                v = body[j]; j += 1
                if v & 0x80: v = ((v & 0x7f) << 8) | body[j]; j += 1
                return v
            idx(); seg = idx()
            if seg == 0: j += 2
            while j < len(body):
                l = body[j]; name = body[j + 1:j + 1 + l].decode('latin-1'); j += 1 + l
                j += 4 if t == 0x91 else 2
                idx(); defined.add(name)
        if t == 0x8C:                    # EXTDEF: (name, type)*
            j = 0
            while j < len(body):
                l = body[j]; name = body[j + 1:j + 1 + l].decode('latin-1'); j += 1 + l
                v = body[j]; j += 2 if v & 0x80 else 1
                if name in defined: bad.append('%s: EXTDEF %s after its PUBDEF' % (fn, name))
        i += 3 + n
    if not d or d[0] != 0x80:
        bad.append('%s: no THEADR record first' % fn)
for b in bad: print('omfcheck:', b)
if bad: sys.exit(1)
print('omfcheck: ok')
