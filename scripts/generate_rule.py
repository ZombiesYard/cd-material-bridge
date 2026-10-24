"""Generate an opt-in rule from an uncompressed, final mounted DDS copy.
No game writes; Python is an authoring tool and not a runtime dependency.
"""
import argparse, hashlib, struct
from pathlib import Path
def make_rule(data,texture,name,base_flags):
    texture=texture.replace('\\','/').lower().lstrip('/')
    if not texture.startswith('character/texture/') or '..' in texture or not texture.endswith('.dds'):
        raise ValueError('Expected a character/texture/*.dds virtual path')
    if len(data)<128 or data[:4]!=b'DDS ' or struct.unpack_from('<I',data,4)[0]!=124:
        raise ValueError('Not a DDS')
    h,w=struct.unpack_from('<II',data,12); mips=struct.unpack_from('<I',data,28)[0]
    fourcc=data[84:88].decode('ascii')
    if fourcc not in ('DXT3','DXT5') or not 0<w<=16384 or not 0<h<=16384 or not 0<mips<=15:
        raise ValueError('v0.1 supports BC2/BC3 cutout textures with valid dimensions and mips')
    expected=sum(max(1,((w>>i)+3)//4)*max(1,((h>>i)+3)//4)*16 for i in range(mips))
    if len(data)!=128+expected: raise ValueError('DDS pixels are compressed, truncated, or have unsupported trailing data')
    if not 0<=base_flags<=0xffffffff or base_flags&1: raise ValueError('Base flags must have low bit cleared')
    if any(c in name for c in '\r\n[]'): raise ValueError('Invalid profile name')
    return f'''; CD Material Bridge v1 rule. Exact mounted pixel hash; alpha bit only.
; Derived from the final mounted DDS, after any DMM resizing.
[Texture:{name}]
Enabled=1
Path={texture}
Width={w}
Height={h}
Mips={mips}
FourCC={fourcc}
BaseFlags={base_flags}
PixelSHA256={hashlib.sha256(data[128:]).hexdigest()}
'''
if __name__=='__main__':
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('dds',type=Path);a.add_argument('--texture',required=True);a.add_argument('--name',required=True);a.add_argument('--base-flags',required=True,type=lambda s:int(s,0));a.add_argument('--output',required=True,type=Path)
    o=a.parse_args();o.output.write_text(make_rule(o.dds.read_bytes(),o.texture,o.name,o.base_flags),encoding='utf-8')
