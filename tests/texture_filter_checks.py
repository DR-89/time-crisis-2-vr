"""Behavioural checks against resolved, blended reference colours on real GLES."""
import ctypes as C
from PIL import Image


def verify(lib, gles, bind, w, h, pixels, draw, error, output):
    swatch=bind(lib,'tc2_fixture_swatch',None,C.c_int,C.c_float,C.c_float,C.c_int,C.c_int,C.c_int,C.c_int)
    filtering=bind(lib,'tc2_fixture_filter',None,C.c_int)
    centre=((h//2)*w+w//2)*3
    def sample(mode,u,v,on,stencil=0,overlay=0,alpha=-1,blend=0):
        filtering(on);swatch(mode,u,v,stencil,overlay,alpha,blend)
        assert draw(w,h,0,0,0,pixels) and error()==0
        return tuple(pixels[centre:centre+3])
    point=sample(0,8,8,0);smooth=sample(0,8,8,1)
    assert point!=smooth
    for on,expected in ((0,point),(1,smooth),(0,point)):
        filtering(on)  # No swatch/geometry upload between toggles.
        assert draw(w,h,0,0,0,pixels) and error()==0
        assert tuple(pixels[centre:centre+3])==expected,'Live cached-frame toggle'
    count=0
    for mode in range(16):
        for tile in range(8):
            # Half-way between four texels; includes every flip/transpose and
            # crossing into the next independently mapped 16x16 tile.
            for u,v in ((tile*16+7,8),(tile*16+16,8)):
                reference=[sample(mode,u+dx,v+dy,0) for dx,dy in ((-.5,-.5),(.5,-.5),(-.5,.5),(.5,.5))]
                actual=sample(mode,u,v,1)
                expected=tuple(sum(c[i] for c in reference)/4 for i in range(3))
                assert max(abs(a-b) for a,b in zip(actual,expected))<=2,(mode,tile,u,actual,expected)
                count+=1
    # A transparent coloured pen must contribute no colour halo. The filtered
    # result should equal averaging colours already blended onto the background.
    for alpha,blend in ((0,0),(0,1),(-1,1)):
        endpoints=[sample(0,u,.5,0,alpha=alpha,blend=blend) for u in (.5,1.5)]
        actual=sample(0,1,.5,1,alpha=alpha,blend=blend)
        assert max(abs(actual[i]-sum(c[i] for c in endpoints)/2) for i in range(3))<=2
    for extra in ({'stencil':1},{'overlay':1}):
        assert sample(0,8,8,0,**extra)==sample(0,8,8,1,**extra)
    # Small motion across a texel boundary must produce a smooth ramp, instead
    # of the single abrupt colour jump that causes texture shimmer in VR.
    ramps=[]
    for on in (0,1):
        ramp=[sample(0,.5+i/32,.5,on) for i in range(33)]
        ramps.append(max(max(abs(a-b) for a,b in zip(x,y)) for x,y in zip(ramp,ramp[1:])))
    assert ramps[1]<ramps[0]/8,ramps
    Image.frombytes('RGB',(w,h),bytes(pixels)).save(output/'synthetic-filter.png')
    print(f'PASS: {count} palette/flip/tile-edge comparisons, transparent pens, blend, stencil and overlay. Largest motion step: nearest {ramps[0]}, bilinear {ramps[1]}.')
