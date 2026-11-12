import zlib,struct
def read(fn):
    d=open(fn,'rb').read(); p=8; idat=b''
    while p<len(d):
        l,t=struct.unpack('>I4s',d[p:p+8]); c=d[p+8:p+8+l]; p+=12+l
        if t==b'IHDR': w,h,bd,ct,_,_,il=struct.unpack('>IIBBBBB',c)
        elif t==b'IDAT': idat+=c
    raw=zlib.decompress(idat); bpp={2:3,6:4}[ct]; s=w*bpp; out=[]; prev=bytearray(s); i=0
    for y in range(h):
        f=raw[i]; i+=1; line=bytearray(raw[i:i+s]); i+=s
        for x in range(s):
            a=line[x-bpp] if x>=bpp else 0; b=prev[x]; c=prev[x-bpp] if x>=bpp else 0
            if f==1: line[x]=(line[x]+a)&255
            elif f==2: line[x]=(line[x]+b)&255
            elif f==3: line[x]=(line[x]+((a+b)>>1))&255
            elif f==4:
                pp=a+b-c; pa,pb,pc=abs(pp-a),abs(pp-b),abs(pp-c)
                line[x]=(line[x]+(a if pa<=pb and pa<=pc else b if pb<=pc else c))&255
        out.append(bytes(line)); prev=line
    return w,h,bpp,out
def px(img,x,y):
    w,h,bpp,rows=img; r=rows[y]; return tuple(r[x*bpp:x*bpp+3])
