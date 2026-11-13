"""Small newc-style reader/writer for local RAM boot construction."""
def parse(b):
 d={};p=0
 while p+110<=len(b):
  f=[int(b[p+6+i*8:p+14+i*8],16) for i in range(13)];n=b[p+110:p+109+f[11]].decode();p=(p+110+f[11]+3)&~3;x=b[p:p+f[6]];p=(p+f[6]+3)&~3
  if n=='TRAILER!!!':return d
  d[n]=[f,x]
 return d
def pack(d):
 b=bytearray()
 for n,(f,x) in list(d.items())+[('TRAILER!!!',([0,0,0,0,1,0,0,0,0,0,0,0,0],b''))]:
  f=f.copy();n=n.encode()+b'\0';f[6]=len(x);f[11]=len(n);b+=b'070701'+b''.join(f'{v:08x}'.encode() for v in f)+n;b+=b'\0'*(-len(b)%4);b+=x;b+=b'\0'*(-len(b)%4)
 b+=b'\0'*(-len(b)%512);return bytes(b)
