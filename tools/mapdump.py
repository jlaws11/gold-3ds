"""Build a Gold RAM snapshot (0x8000-0xFFFF) for any map, straight from the ROM,
so the 3D builder can be checked on every area without playing there.

Usage: mapdump.py gold.gbc pokegold.sym maps.asm out_dir [name-filter]
Writes <out_dir>/<group>_<map>_<Name>.bin for each map and prints a list.
Map names and grouping come from pret/pokegold data/maps/maps.asm.
"""
import os, re, sys

rom = open(sys.argv[1], 'rb').read()
syms = {}
for l in open(sys.argv[2]):
    p = l.split()
    if len(p) == 2 and ':' in p[0]:
        b, a = p[0].split(':'); syms[p[1]] = (int(b, 16), int(a, 16))
out_dir = sys.argv[4]; flt = sys.argv[5] if len(sys.argv) > 5 else None
os.makedirs(out_dir, exist_ok=True)

def off(bank, addr): return addr if addr < 0x4000 else bank * 0x4000 + addr - 0x4000
def r16(o): return rom[o] | rom[o + 1] << 8

def lz3(o):
    """Gold/Crystal LZ (home/decompress.asm)"""
    out = bytearray()
    while True:
        b = rom[o]; o += 1
        if b == 0xFF: return bytes(out)
        cmd = b >> 5
        if cmd == 7:
            cmd = (b >> 2) & 7; n = ((b & 3) << 8 | rom[o]) + 1; o += 1
        else:
            n = (b & 31) + 1
        if cmd == 0: out += rom[o:o + n]; o += n
        elif cmd == 1: out += bytes([rom[o]]) * n; o += 1
        elif cmd == 2:
            a, c = rom[o], rom[o + 1]; o += 2
            out += bytes([a if i % 2 == 0 else c for i in range(n)])
        elif cmd == 3: out += bytes(n)
        else:
            x = rom[o]; o += 1
            if x & 0x80: src = len(out) - (x & 0x7F) - 1
            else: src = (x << 8) | rom[o]; o += 1
            for i in range(n):
                if cmd == 4: out.append(out[src + i])
                elif cmd == 5:
                    v = out[src + i]; out.append(int('{:08b}'.format(v)[::-1], 2))
                else: out.append(out[src - i])

# map list from the decomp, in group order
groups, cur = [], None
for l in open(sys.argv[3]):
    m = re.match(r'MapGroup_(\w+):', l)
    if m: cur = []; groups.append((m.group(1), cur)); continue
    m = re.match(r'\s+map (\w+), (\w+), (\w+)', l)
    if m and cur is not None: cur.append(m.group(1))

mgp = off(*syms['MapGroupPointers']); tsets = off(*syms['Tilesets'])
roofs_by_group = off(*syms['MapGroupRoofs']); roofs = off(*syms['Roofs'])
ENV = {1: 'TOWN', 2: 'ROUTE', 3: 'INDOOR', 4: 'CAVE', 5: 'ENV5', 6: 'GATE', 7: 'DUNGEON'}

listing = []
for gi, (gname, maps) in enumerate(groups, start=1):
    gptr = off(0x25, r16(mgp + (gi - 1) * 2))
    for mi, name in enumerate(maps, start=1):
        if flt and not re.search(flt, name): continue
        h = gptr + (mi - 1) * 9
        abank, tileset, env = rom[h], rom[h + 1], rom[h + 2]
        attr = off(abank, r16(h + 3))
        border, height, width = rom[attr], rom[attr + 1], rom[attr + 2]
        bbank, bptr = rom[attr + 3], r16(attr + 4)
        blocks = rom[off(bbank, bptr):off(bbank, bptr) + width * height]
        t = tsets + tileset * 15
        gfx = lz3(off(rom[t], r16(t + 1)))
        mem = bytearray(0x8000)
        def w(a, v): mem[a - 0x8000] = v
        mem[0x1000:0x1000 + min(len(gfx), 0x800)] = gfx[:0x800]          # vTiles2 = 0x9000
        rf = rom[roofs_by_group + gi]
        if rf != 0xFF and tileset in (1, 2):                              # TILESET_JOHTO(_MODERN)
            src = roofs + rf * 9 * 16
            mem[0x1000 + 0x0A * 16:0x1000 + 0x13 * 16] = rom[src:src + 9 * 16]
        stride = width + 6
        for y in range(height + 6):
            for x in range(stride):
                bx, by = x - 3, y - 3
                v = blocks[by * width + bx] if 0 <= bx < width and 0 <= by < height else border
                w(0xC700 + y * stride + x, v)
        w(0xD082, tileset); w(0xD083, env); w(0xD087, height); w(0xD088, width)
        w(0xD0C5, rom[t + 3]); w(0xD0C6, rom[t + 4]); w(0xD0C7, rom[t + 5])
        w(0xD0C8, rom[t + 6]); w(0xD0C9, rom[t + 7]); w(0xD0CA, rom[t + 8])
        w(0xD0CF, rom[t + 13]); w(0xD0D0, rom[t + 14])
        w(0xDA00, gi); w(0xDA01, mi)
        w(0xDA03, width); w(0xDA02, height)                                # player in the middle
        w(0xD568, 1); w(0xFF96, 12); w(0xFF98, 0); w(0xD159, 2)
        fn = f'{out_dir}/{gi:02d}_{mi:02d}_{name}.bin'
        open(fn, 'wb').write(mem)
        listing.append((fn, name, tileset, ENV.get(env, env), width, height))
for x in listing: print(*x)
