import struct, sys, os, subprocess

CREATOR   = b'NCls'
FLAGS_SET = 0x2000 | 0x0400  # hasBundle | hasCustomIcon

path = sys.argv[1]  # e.g. %NotionClassic.ad

# --- Patch the AppleDouble (.ad) file ---
with open(path, 'r+b') as f:
    data = bytearray(f.read())
n = struct.unpack_from('>H', data, 24)[0]
for i in range(n):
    eid, off, l = struct.unpack_from('>III', data, 26 + i*12)
    if eid == 9:
        data[off+4:off+8] = CREATOR
        fo = off + 8
        fl = struct.unpack_from('>H', data, fo)[0] | FLAGS_SET
        struct.pack_into('>H', data, fo, fl)
with open(path, 'wb') as f:
    f.write(data)

# --- Also patch the xattrs on the .APPL file (BasiliskII reads these) ---
# Derive: %NotionClassic.ad -> NotionClassic.APPL
dirname  = os.path.dirname(os.path.abspath(path))
basename = os.path.basename(path)         # %NotionClassic.ad
appname  = basename.lstrip('%')           # NotionClassic.ad
appname  = os.path.splitext(appname)[0]  # NotionClassic
appl     = os.path.join(dirname, appname + '.APPL')

def get_xattr_bytes(p, name):
    r = subprocess.run(['xattr', '-px', name, p], capture_output=True, text=True)
    if r.returncode != 0:
        return None
    return bytes(int(x, 16) for x in r.stdout.split())

def set_xattr_bytes(p, name, raw):
    hex_str = ''.join(f'{b:02X}' for b in raw)
    subprocess.run(['xattr', '-wx', name, hex_str, p], capture_output=True)

def patch_finfo(raw):
    raw = bytearray(raw)
    raw[4:8] = CREATOR
    fl = struct.unpack_from('>H', raw, 8)[0] | FLAGS_SET
    struct.pack_into('>H', raw, 8, fl)
    return bytes(raw)

if os.path.exists(appl):
    for attr in ('org.BasiliskII.FinderInfo', 'com.apple.FinderInfo'):
        raw = get_xattr_bytes(appl, attr)
        if raw and len(raw) >= 10:
            set_xattr_bytes(appl, attr, patch_finfo(raw))
