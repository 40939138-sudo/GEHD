"""DDS layout and identity utilities extracted unchanged from texture_pack/extract.py."""
import struct,hashlib

def sha(b): return hashlib.sha256(b).hexdigest().upper()
def dds_info(b):
    if len(b)<128 or b[:4]!=b'DDS ': raise ValueError('DDS_HEADER')
    u=lambda o:struct.unpack_from('<I',b,o)[0]
    if u(4)!=124 or u(76)!=32: raise ValueError('DDS_HEADER_SIZE')
    w,h,m=u(16),u(12),max(1,u(28))
    if not 0<w<=16384 or not 0<h<=16384 or m>max(w,h).bit_length(): raise ValueError('DDS_DIMENSIONS')
    if u(112) or u(24)>1: raise ValueError('DDS_NOT_2D')
    fmt=b[84:88].decode('ascii',errors='replace') if u(80)&4 else 'RGB'+str(u(88))
    block={'DXT1':8,'DXT2':16,'DXT3':16,'DXT4':16,'DXT5':16,'ATI1':8,'ATI2':16}.get(fmt)
    if block: sizes=[max(1,(max(1,w>>i)+3)//4)*max(1,(max(1,h>>i)+3)//4)*block for i in range(m)]
    elif not u(80)&4 and u(88) in (8,16,24,32):
        sizes=[max(1,w>>i)*max(1,h>>i)*(u(88)//8) for i in range(m)]
    else: raise ValueError('DDS_FORMAT_'+fmt)
    if 128+sum(sizes)!=len(b): raise ValueError('DDS_LENGTH')
    return dict(width=w,height=h,mips=m,format=fmt,bytes=len(b),levelBytes=sizes)
