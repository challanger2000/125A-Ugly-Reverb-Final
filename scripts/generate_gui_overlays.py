#!/usr/bin/env python3
import math
import os
import random
import struct
import sys
import zlib

BASE_W, BASE_H = 760, 430
SCALE = 1
W, H = BASE_W, BASE_H

def set_scale(scale):
    global SCALE, W, H
    SCALE = scale
    W, H = BASE_W * SCALE, BASE_H * SCALE

def blank():
    return bytearray(W * H * 4)

def blend(buf, x, y, rgba):
    if x < 0 or y < 0 or x >= W or y >= H:
        return
    r,g,b,a = rgba
    if a <= 0:
        return
    i=(y*W+x)*4
    da=buf[i+3]
    sa=a/255.0
    dda=da/255.0
    oa=sa + dda*(1.0-sa)
    if oa <= 0:
        return
    for c,sv in enumerate((r,g,b)):
        dv=buf[i+c]
        buf[i+c]=max(0,min(255,round((sv*sa + dv*dda*(1-sa))/oa)))
    buf[i+3]=max(0,min(255,round(oa*255)))

def line(buf, x0,y0,x1,y1,rgba,width=1,broken=False,rng=None):
    dx=x1-x0; dy=y1-y0
    steps=max(1,int(max(abs(dx),abs(dy))))
    rng=rng or random
    phys_width=max(1,round(width*SCALE))
    for s in range(steps+1):
        if broken and rng.random()<0.18:
            continue
        t=s/steps
        x=round((x0+dx*t)*SCALE); y=round((y0+dy*t)*SCALE)
        for yy in range(y-phys_width//2,y+phys_width//2+1):
            for xx in range(x-phys_width//2,x+phys_width//2+1):
                blend(buf,xx,yy,rgba)

def blob(buf,cx,cy,rx,ry,rgba,seed):
    rng=random.Random(seed)
    jitter=[rng.uniform(0.72,1.18) for _ in range(32)]
    pcx,pcy,prx,pry=cx*SCALE,cy*SCALE,rx*SCALE,ry*SCALE
    x0=max(0,int(pcx-prx-2*SCALE)); x1=min(W,int(pcx+prx+3*SCALE))
    y0=max(0,int(pcy-pry-2*SCALE)); y1=min(H,int(pcy+pry+3*SCALE))
    for y in range(y0,y1):
        for x in range(x0,x1):
            dx=(x-pcx)/max(1.0,prx); dy=(y-pcy)/max(1.0,pry)
            d=math.sqrt(dx*dx+dy*dy)
            ang=(math.atan2(dy,dx)+math.pi)/(2*math.pi)
            j=jitter[int(ang*31.999)]
            edge=j
            if d<edge:
                fade=max(0.0,min(1.0,(edge-d)/0.22))
                a=round(rgba[3]*fade)
                blend(buf,x,y,(rgba[0],rgba[1],rgba[2],a))

def soft_ellipse(buf,cx,cy,rx,ry,rgba):
    pcx,pcy,prx,pry=cx*SCALE,cy*SCALE,rx*SCALE,ry*SCALE
    x0=max(0,int(pcx-prx*2)); x1=min(W,int(pcx+prx*2)+1)
    y0=max(0,int(pcy-pry*2)); y1=min(H,int(pcy+pry*2)+1)
    for y in range(y0,y1):
        for x in range(x0,x1):
            dx=(x-pcx)/max(1.0,prx); dy=(y-pcy)/max(1.0,pry)
            d2=dx*dx+dy*dy
            if d2>4.0: continue
            a=round(rgba[3]*math.exp(-1.55*d2))
            if a:
                blend(buf,x,y,(rgba[0],rgba[1],rgba[2],a))

def png_chunk(tag,data):
    return struct.pack(">I",len(data))+tag+data+struct.pack(">I",zlib.crc32(tag+data)&0xffffffff)

def write_png(path,buf):
    raw=bytearray()
    stride=W*4
    for y in range(H):
        raw.append(0)
        raw.extend(buf[y*stride:(y+1)*stride])
    payload=(b"\x89PNG\r\n\x1a\n"+
             png_chunk(b"IHDR",struct.pack(">IIBBBBB",W,H,8,6,0,0,0))+
             png_chunk(b"IDAT",zlib.compress(bytes(raw),9))+
             png_chunk(b"IEND",b""))
    with open(path,"wb") as f:
        f.write(payload)

def mask_wear_to_panel_interiors(buf, panels, inset=6, radius=7):
    # Keep wear strictly inside the blue module faces so the blue border remains clean.
    def inside_round_rect(x,y,l,t,r,b,rad):
        if x < l or x >= r or y < t or y >= b:
            return False
        if l+rad <= x < r-rad or t+rad <= y < b-rad:
            return True
        cx = l+rad if x < l+rad else r-rad-1
        cy = t+rad if y < t+rad else b-rad-1
        dx=x-cx; dy=y-cy
        return dx*dx+dy*dy <= rad*rad

    for y in range(H):
        ly=y/SCALE
        for x in range(W):
            lx=x/SCALE
            keep=False
            for l,t,r,b in panels:
                il, it, ir, ib = l+inset, t+inset, r-inset, b-inset
                if inside_round_rect(lx,ly,il,it,ir,ib,radius):
                    keep=True
                    break
            if not keep:
                i=(y*W+x)*4
                buf[i+3]=0
    return buf

def generate_wear():
    buf=blank()
    panels=[(16,70,278,414),(288,70,618,414),(628,70,744,414)]
    rng=random.Random(12501)

    # Each panel gets a few deliberate wear zones instead of evenly distributed noise.
    cluster_specs=[
        # left: lower-left, upper-right and one small mid cluster
        [(42,382,34,18,1.0),(245,91,26,12,0.85),(82,214,16,24,0.65)],
        # center: strongest, with asymmetrical edge wear and two interior zones
        [(305,392,42,16,1.25),(590,92,34,13,1.10),(360,218,24,18,0.70),(548,304,28,19,0.78)],
        # master: restrained wear, mostly top/right and lower edge
        [(724,96,18,26,0.85),(650,392,25,12,0.72),(706,236,14,18,0.55)]
    ]

    for pi,(l,t,r,b) in enumerate(panels):
        clusters=cluster_specs[pi]

        # Irregular edge chips, concentrated near cluster positions.
        edge_count=22 if pi==1 else 14
        for n in range(edge_count):
            ccx,ccy,crx,cry,weight=rng.choice(clusters)
            # choose nearest panel edge to the cluster and pull the mark toward it
            distances=[abs(ccy-t),abs(ccy-b),abs(ccx-l),abs(ccx-r)]
            side=distances.index(min(distances))
            if side==0:
                cx=max(l+6,min(r-6,rng.gauss(ccx,crx*0.55))); cy=t+rng.uniform(0,3.5)
                rx=rng.uniform(2,8); ry=rng.uniform(1,3.5)
            elif side==1:
                cx=max(l+6,min(r-6,rng.gauss(ccx,crx*0.55))); cy=b-rng.uniform(0,3.5)
                rx=rng.uniform(2,8); ry=rng.uniform(1,3.5)
            elif side==2:
                cx=l+rng.uniform(0,3.5); cy=max(t+6,min(b-6,rng.gauss(ccy,cry*0.55)))
                rx=rng.uniform(1,3.5); ry=rng.uniform(2,8)
            else:
                cx=r-rng.uniform(0,3.5); cy=max(t+6,min(b-6,rng.gauss(ccy,cry*0.55)))
                rx=rng.uniform(1,3.5); ry=rng.uniform(2,8)
            alpha=round(rng.uniform(36,74)*weight)
            col=(183,191,194,alpha)
            if rng.random()<0.34:
                col=(120,69,40,round(alpha*0.92))
            blob(buf,cx,cy,rx,ry,col,12500+pi*100+n)

        # Clustered interior flecks, leaving large clean areas.
        fleck_count=18 if pi==1 else 10
        for n in range(fleck_count):
            ccx,ccy,crx,cry,weight=rng.choice(clusters)
            cx=max(l+10,min(r-10,rng.gauss(ccx,crx)))
            cy=max(t+12,min(b-12,rng.gauss(ccy,cry)))
            rx=rng.uniform(0.7,2.0); ry=rng.uniform(0.6,1.7)
            alpha=round(rng.uniform(18,40)*weight)
            col=(148,153,151,alpha)
            if rng.random()<0.30:
                col=(131,77,45,min(52,alpha+7))
            blob(buf,cx,cy,rx,ry,col,13000+pi*100+n)

        # Sparse scratches only near selected wear zones.
        scratch_count=5 if pi==1 else 3
        for n in range(scratch_count):
            ccx,ccy,crx,cry,weight=rng.choice(clusters)
            x=max(l+12,min(r-46,rng.gauss(ccx,crx)))
            y=max(t+18,min(b-18,rng.gauss(ccy,cry)))
            ln=rng.uniform(12,36)
            slope=rng.uniform(-0.16,0.16)
            line(buf,x,y,x+ln,y+slope*ln,(214,220,222,rng.randint(18,40)),1,True,rng)

    return mask_wear_to_panel_interiors(buf, panels, inset=6, radius=7)

def generate_glass():
    buf=blank()
    # broad soft reflection bands; calculated per pixel so there are no hard vector edges
    bands=[(88,0.34,22,10),(545,0.17,15,6)]
    for y in range(H):
        ly=y/SCALE
        for x in range(W):
            lx=x/SCALE
            a=0.0
            for center,slope,sigma,maxa in bands:
                d=lx-(center+slope*ly)
                a+=maxa*math.exp(-(d*d)/(2*sigma*sigma))
            if a>0.35:
                blend(buf,x,y,(238,245,249,min(18,round(a))))
    rng=random.Random(12502)
    # broad matte wipe traces / fingerprint-like haze
    for n in range(17):
        soft_ellipse(buf,rng.uniform(25,BASE_W-25),rng.uniform(22,BASE_H-22),
                     rng.uniform(22,72),rng.uniform(7,25),
                     (226,235,240,rng.randint(2,6)))
    for n in range(5):
        soft_ellipse(buf,rng.uniform(40,BASE_W-40),rng.uniform(25,BASE_H-25),
                     rng.uniform(18,45),rng.uniform(8,20),
                     (7,12,16,rng.randint(2,4)))
    # very fine cover scratches, sparse and broken
    for n in range(13):
        x=rng.uniform(25,BASE_W-95); y=rng.uniform(18,BASE_H-18)
        ln=rng.uniform(20,70); slope=rng.uniform(-0.16,0.16)
        line(buf,x,y,x+ln,y+slope*ln,(242,247,250,rng.randint(8,18)),1,True,rng)
    # subtle dusty edge haze
    for n in range(70):
        side=rng.randrange(4)
        if side<2:
            x=rng.uniform(0,BASE_W); y=rng.uniform(0,9) if side==0 else rng.uniform(BASE_H-9,BASE_H)
        else:
            x=rng.uniform(0,9) if side==2 else rng.uniform(BASE_W-9,BASE_W); y=rng.uniform(0,BASE_H)
        blob(buf,x,y,rng.uniform(0.7,2.3),rng.uniform(0.7,2.3),
             (220,228,232,rng.randint(3,10)),15000+n)
    return buf

def main():
    out=sys.argv[1] if len(sys.argv)>1 else "."
    os.makedirs(out,exist_ok=True)

    set_scale(1)
    write_png(os.path.join(out,"ugly_wear_overlay.png"),generate_wear())
    write_png(os.path.join(out,"ugly_glass_overlay.png"),generate_glass())

    set_scale(2)
    write_png(os.path.join(out,"ugly_wear_overlay#2.0x.png"),generate_wear())
    write_png(os.path.join(out,"ugly_glass_overlay#2.0x.png"),generate_glass())

    print("Generated 1x and 2x RGBA GUI overlays")

if __name__=="__main__":
    main()
