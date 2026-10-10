"""Bake the user's multipart Tripo GLB into one Quest/PCVR textured draw.

Preserves scene transforms, simplifies each material separately (no texture
bleeding across parts), packs padded texture tiles and calibrates the aim axis.
Runtime format remains TCGUN001; no new runtime dependencies are introduced.
"""
from pathlib import Path
import argparse,hashlib,io,json,struct,subprocess,tempfile
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
SOURCE_SHA='391118669dfa126ae128fe51f9d8dde9d4fb8f506032a91a044c578f068df02c'
CALIBRATION={
    'source_forward':[-.2472818,.19851744,-.94838945],
    'source_muzzle':[-.12247651008849472,.47063976771123506,-.48472601657833037],
    'source_grip':[.029682405292987823,.15491321682929993,.09414516389369965],
    'length_m':.45,'grip_z_m':.025,
}

def read_glb(path):
    raw=path.read_bytes();assert struct.unpack_from('<III',raw)==(0x46546c67,2,len(raw))
    at=12;doc=None;binary=None
    while at<len(raw):
        size,kind=struct.unpack_from('<II',raw,at);at+=8
        chunk=raw[at:at+size];assert len(chunk)==size;at+=size
        if kind==0x4e4f534a:doc=json.loads(chunk)
        elif kind==0x004e4942:binary=chunk
    assert doc and binary and not doc.get('extensionsRequired')
    assert len(doc['buffers'])==1 and len(binary)>=doc['buffers'][0]['byteLength']
    def accessor(i):
        a=doc['accessors'][i];assert not a.get('sparse') and not a.get('normalized')
        v=doc['bufferViews'][a['bufferView']];assert v.get('buffer',0)==0
        dt=np.dtype({5121:'u1',5123:'<u2',5125:'<u4',5126:'<f4'}[a['componentType']])
        width={'SCALAR':1,'VEC2':2,'VEC3':3}[a['type']]
        start=a.get('byteOffset',0);stride=v.get('byteStride',dt.itemsize*width)
        assert a['count']>0 and start+(a['count']-1)*stride+width*dt.itemsize<=v['byteLength']
        data=np.ndarray((a['count'],width),dt,buffer=binary,offset=v.get('byteOffset',0)+start,strides=(stride,dt.itemsize)).copy()
        assert np.isfinite(data).all();return data
    def image(i):
        im=doc['images'][i];v=doc['bufferViews'][im['bufferView']];start=v.get('byteOffset',0)
        return Image.open(io.BytesIO(binary[start:start+v['byteLength']])).convert('RGBA')
    return raw,doc,accessor,image

def node_matrix(node):
    if 'matrix' in node:return np.array(node['matrix'],dtype=float).reshape(4,4).T
    x,y,z,w=node.get('rotation',[0,0,0,1])
    r=np.array([[1-2*y*y-2*z*z,2*x*y-2*z*w,2*x*z+2*y*w],
                [2*x*y+2*z*w,1-2*x*x-2*z*z,2*y*z-2*x*w],
                [2*x*z-2*y*w,2*y*z+2*x*w,1-2*x*x-2*y*y]])
    m=np.eye(4);m[:3,:3]=r@np.diag(node.get('scale',[1,1,1]));m[:3,3]=node.get('translation',[0,0,0]);return m

def load_parts(doc,accessor):
    parts=[]
    def visit(i,parent,ancestors):
        assert i not in ancestors,'Cyclic scene graph'
        node=doc['nodes'][i];matrix=parent@node_matrix(node)
        if 'mesh' in node:
            assert 'skin' not in node
            for primitive in doc['meshes'][node['mesh']]['primitives']:
                assert primitive.get('mode',4)==4 and not primitive.get('targets')
                a=primitive['attributes'];p=accessor(a['POSITION']);n=accessor(a['NORMAL']);uv=accessor(a['TEXCOORD_0']);ind=accessor(primitive['indices']).reshape(-1).astype('<u4')
                assert len(p)==len(n)==len(uv) and len(ind)%3==0 and ind.max()<len(p)
                assert uv.min()>=-1e-5 and uv.max()<=1.00001,'Repeating UVs need an explicit bake'
                p=p@matrix[:3,:3].T+matrix[:3,3];n=n@np.linalg.inv(matrix[:3,:3]);n/=np.maximum(np.linalg.norm(n,axis=1,keepdims=True),1e-8)
                if np.linalg.det(matrix[:3,:3])<0:ind=ind.reshape(-1,3)[:,[0,2,1]].reshape(-1)
                parts.append({'name':node.get('name',str(i)),'pos':p,'normal':n,'uv':uv,'indices':ind,'material':primitive['material']})
        for child in node.get('children',[]):visit(child,matrix,ancestors|{i})
    for i in doc['scenes'][doc.get('scene',0)]['nodes']:visit(i,np.eye(4),set())
    return parts

def pack_textures(doc,get_image,size=2048):
    images={}
    for i,m in enumerate(doc['materials']):
        assert m.get('alphaMode','OPAQUE')=='OPAQUE'
        pbr=m['pbrMetallicRoughness'];texture=pbr['baseColorTexture'];assert texture.get('texCoord',0)==0 and not texture.get('extensions')
        pixels=np.asarray(get_image(doc['textures'][texture['index']]['source']),dtype=float)
        pixels*=np.array(pbr.get('baseColorFactor',[1,1,1,1]));images[i]=Image.fromarray(np.clip(pixels,0,255).astype('uint8'))
    padding=8
    scale=min(1,(size*size*.78/sum(im.width*im.height for im in images.values()))**.5)
    for attempt in range(60):
        order=sorted(images,key=lambda i:(-images[i].height,-images[i].width,i));placements={};x=y=row_h=0
        for i in order:
            w=max(4,int(images[i].width*scale));h=max(4,int(images[i].height*scale))
            if x+w+2*padding>size:x=0;y+=row_h;row_h=0
            if y+h+2*padding>size:break
            placements[i]=(x+padding,y+padding,w,h);x+=w+2*padding;row_h=max(row_h,h+2*padding)
        if len(placements)==len(images):break
        scale*=.95
    else:raise RuntimeError('Cannot pack texture atlas')
    atlas=Image.new('RGBA',(size,size),(24,24,24,255))
    for i,(x,y,w,h) in placements.items():
        im=images[i].resize((w,h),Image.Resampling.LANCZOS)
        padded=np.pad(np.asarray(im),((padding,padding),(padding,padding),(0,0)),mode='edge')
        atlas.paste(Image.fromarray(padded),(x-padding,y-padding))
    return atlas,placements

def convert(source,destination,target):
    raw,doc,accessor,get_image=read_glb(source)
    digest=hashlib.sha256(raw).hexdigest()
    assert digest==SOURCE_SHA,'This calibration is specific to the inspected Tripo export'
    parts=load_parts(doc,accessor);original_count=sum(len(p['indices'])//3 for p in parts)
    atlas,tiles=pack_textures(doc,get_image)
    f=np.array(CALIBRATION['source_forward']);f/=np.linalg.norm(f);z=-f;x=np.cross([0,1,0],z);x/=np.linalg.norm(x);y=np.cross(z,x);rotation=np.array([x,y,z])
    all_pos=np.vstack([p['pos'] for p in parts])@rotation.T
    scale=CALIBRATION['length_m']/np.ptp(all_pos[:,2]);tip=np.array(CALIBRATION['source_muzzle'])@rotation.T
    grip=np.array(CALIBRATION['source_grip'])@rotation.T
    origin=np.array([tip[0],tip[1],grip[2]-CALIBRATION['grip_z_m']/scale])
    tip=(tip-origin)*scale
    for p in parts:
        p['pos']=(p['pos']@rotation.T-origin)*scale;p['normal']=p['normal']@rotation.T
    if target:
        job={'parts':[{'positions':p['pos'].reshape(-1).tolist(),'attributes':np.concatenate([p['normal'],p['uv']],axis=1).reshape(-1).tolist(),'indices':p['indices'].tolist(),'target':max(12,round(target*(len(p['indices'])/3)/original_count))} for p in parts]}
        with tempfile.TemporaryDirectory(prefix='gun-import-',dir=ROOT/'build') as tmp:
            src=Path(tmp)/'job.json';dst=Path(tmp)/'result.json';src.write_text(json.dumps(job),encoding='utf-8')
            subprocess.run(['node',str(ROOT/'tools/simplify_gun.mjs'),str(src),str(dst)],check=True,cwd=ROOT)
            results=json.loads(dst.read_text())
        for p,result in zip(parts,results):p['indices']=np.array(result['indices'],dtype='<u4');p['error']=result['error']
    vertices=[];indices=[];base=0
    for p in parts:
        used,remap=np.unique(p['indices'],return_inverse=True);ind=remap.astype('<u4')
        uv=np.clip(p['uv'][used],0,1).copy();tx,ty,w,h=tiles[p['material']]
        uv[:,0]=(tx+uv[:,0]*w)/atlas.width;uv[:,1]=(ty+uv[:,1]*h)/atlas.height
        v=np.concatenate([p['pos'][used],p['normal'][used],uv],axis=1).astype('<f4')
        vertices.append(v);indices.append(ind+base);base+=len(v)
    vertices=np.vstack(vertices);indices=np.concatenate(indices)
    assert 0<len(vertices)<=100000 and 0<len(indices)<=300000 and np.isfinite(vertices).all()
    bounds=[vertices[:,:3].min(0).tolist(),vertices[:,:3].max(0).tolist()]
    assert .44<=bounds[1][2]-bounds[0][2]<=.46 and -.4<tip[2]<-.15
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_bytes(struct.pack('<8sIIII3f',b'TCGUN001',len(vertices),len(indices),*atlas.size,*tip)+vertices.tobytes()+indices.tobytes()+atlas.tobytes())
    report={'generator':'Tripo3D','source_file':source.name,'source_sha256':digest,'asset_sha256':hashlib.sha256(destination.read_bytes()).hexdigest(),'format':'TCGUN001','source_parts':len(parts),'source_triangles':original_count,'vertices':len(vertices),'triangles':len(indices)//3,'albedo_size':list(atlas.size),'atlas_padding_px':8,'bounds_m':bounds,'muzzle_m':tip.tolist(),'calibration':CALIBRATION,'simplifier':'meshoptimizer 1.3.0' if target else None,'target_triangles':target,'max_part_error':max(p.get('error',0) for p in parts),'parts':[{'name':p['name'],'triangles':len(p['indices'])//3,'error':p.get('error',0)} for p in parts]}
    destination.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='parts'},indent=2))

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('source',type=Path);parser.add_argument('destination',type=Path);parser.add_argument('--triangles',type=int,default=6000);a=parser.parse_args()
    assert a.triangles>=0;convert(a.source,a.destination,a.triangles)
