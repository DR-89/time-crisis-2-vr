"""Import the inspected Martoscar/Nomad GLB, preserving its full topology.

One textured draw: node transforms, missing hard-surface normals, uniform
vertex colours and base materials are baked offline. Source remains local.
"""
from pathlib import Path
import argparse,hashlib,io,json,struct,zipfile
import numpy as np
from PIL import Image
from import_machine_gun import node_matrix

SOURCE_SHA='989b25eaa2d22a25c424c6bb3d3830eda0a88724099019bf21fe2989d2fe1a02'
SOURCE_URL='https://sketchfab.com/3d-models/namco-arcade-gun-15fbd5b9add94a34b5c21746e3dd32be'
# The model is already 24.75 cm long. +X is barrel-forward; +Y is up.
# Anchor in the upper grip; the actual barrel/lens axis is above this point.
GRIP=np.array([-.05,-.015,0.])
ROTATION=np.array([[0,0,1],[0,1,0],[-1,0,0.]])
MUZZLE=np.array([.123751,.039069425,0.])

def convert(source,destination):
    if source.suffix.lower()=='.zip':
        with zipfile.ZipFile(source) as z:raw=z.read('source/Namco Arcade Gun 1.glb')
    else:raw=source.read_bytes()
    assert hashlib.sha256(raw).hexdigest()==SOURCE_SHA,'Recalibrate a different model export before importing'
    assert struct.unpack_from('<III',raw)==(0x46546c67,2,len(raw))
    size,kind=struct.unpack_from('<II',raw,12);assert kind==0x4e4f534a
    d=json.loads(raw[20:20+size]);binary=raw[28+size:];assert not d.get('extensionsRequired')
    def accessor(i):
        a=d['accessors'][i];assert not a.get('sparse');b=d['bufferViews'][a['bufferView']]
        dt=np.dtype({5121:'u1',5123:'<u2',5125:'<u4',5126:'<f4'}[a['componentType']]);w={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']]
        start=a.get('byteOffset',0);stride=b.get('byteStride',w*dt.itemsize)
        assert start+(a['count']-1)*stride+w*dt.itemsize<=b['byteLength']
        v=np.ndarray((a['count'],w),dt,buffer=binary,offset=b.get('byteOffset',0)+start,strides=(stride,dt.itemsize)).copy()
        if a.get('normalized'):v=v.astype(float)/np.iinfo(dt).max
        assert np.isfinite(v).all();return v
    atlas=Image.new('RGBA',(1024,1024),(255,255,255,255));parts=[];verts=[];indices=[];base=0
    def visit(i,parent):
        nonlocal base
        node=d['nodes'][i];matrix=parent@node_matrix(node)
        if 'mesh' in node:
            for primitive in d['meshes'][node['mesh']]['primitives']:
                assert primitive.get('mode',4)==4 and not primitive.get('targets')
                attrs=primitive['attributes'];p=accessor(attrs['POSITION']);ind=accessor(primitive['indices']).reshape(-1).astype('<u4')
                assert len(ind)%3==0 and ind.max()<len(p)
                if 'NORMAL' in attrs:n=accessor(attrs['NORMAL'])
                else:
                    n=np.zeros_like(p);tri=ind.reshape(-1,3);cross=np.cross(p[tri[:,1]]-p[tri[:,0]],p[tri[:,2]]-p[tri[:,0]])
                    for j in range(3):np.add.at(n,tri[:,j],cross)
                p=p@matrix[:3,:3].T+matrix[:3,3];n=n@np.linalg.inv(matrix[:3,:3]);n=n@ROTATION.T
                n/=np.maximum(np.linalg.norm(n,axis=1,keepdims=True),1e-8);p=(p-GRIP)@ROTATION.T
                assert np.linalg.det(matrix[:3,:3])>0
                uv=accessor(attrs['TEXCOORD_0']).astype(float);assert uv.min()>=0 and uv.max()<=1
                colors=accessor(attrs['COLOR_0']);assert np.allclose(colors,colors[0]),'Nonuniform vertex colours require a UV bake'
                material=primitive['material'];m=d['materials'][material];assert m.get('alphaMode','OPAQUE')=='OPAQUE'
                pbr=m['pbrMetallicRoughness'];factor=np.array(pbr.get('baseColorFactor',[1,1,1,1]))*colors[0]
                if 'baseColorTexture' in pbr:
                    texture=pbr['baseColorTexture'];assert texture.get('texCoord',0)==0 and not texture.get('extensions')
                    image=d['images'][d['textures'][texture['index']]['source']];b=d['bufferViews'][image['bufferView']];off=b.get('byteOffset',0)
                    pixels=np.asarray(Image.open(io.BytesIO(binary[off:off+b['byteLength']])).convert('RGBA').resize((496,496),Image.Resampling.LANCZOS),dtype=float)/255
                    c=pixels[:,:,:3];pixels[:,:,:3]=np.where(c<=.04045,c/12.92,((c+.055)/1.055)**2.4)
                else:pixels=np.ones((496,496,4))
                pixels*=factor;pixels[:,:,3]=1
                tile=np.clip(pixels*255+.5,0,255).astype('uint8');tile=np.pad(tile,((8,8),(8,8),(0,0)),mode='edge')
                tx=(material%2)*512;ty=(material//2)*512;atlas.paste(Image.fromarray(tile),(tx,ty))
                uv[:,0]=(tx+8+uv[:,0]*495+.5)/1024;uv[:,1]=(ty+8+uv[:,1]*495+.5)/1024
                verts.append(np.concatenate([p,n,uv],axis=1).astype('<f4'));indices.append(ind+base);base+=len(p)
                parts.append(dict(name=node['name'],triangles=len(ind)//3,vertices=len(p)))
        for child in node.get('children',[]):visit(child,matrix)
    for i in d['scenes'][d.get('scene',0)]['nodes']:visit(i,np.eye(4))
    v=np.concatenate(verts);ind=np.concatenate(indices).astype('<u4');tip=(MUZZLE-GRIP)@ROTATION.T
    assert len(v)<100000 and len(ind)<300000 and np.isfinite(v).all()
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_bytes(struct.pack('<8sIIII3f',b'TCGUN001',len(v),len(ind),1024,1024,*tip)+v.tobytes()+ind.tobytes()+atlas.tobytes())
    report=dict(model='Namco Arcade Gun',author='Martoscar',license='CC-BY-4.0',license_url='https://creativecommons.org/licenses/by/4.0/',
        source=SOURCE_URL,source_sha256=SOURCE_SHA,asset_sha256=hashlib.sha256(destination.read_bytes()).hexdigest(),
        format='TCGUN001',vertices=len(v),triangles=len(ind)//3,parts=parts,texture_size=[1024,1024],
        source_grip_m=GRIP.tolist(),muzzle_m=tip.tolist(),bounds_m=[v[:,:3].min(0).tolist(),v[:,:3].max(0).tolist()],
        modifications='Baked transforms/materials/vertex colours, reconstructed missing normals, calibrated grip and muzzle. Full source topology; lightweight directional lighting replaces PBR/normal maps; whole-gun recoil.')
    destination.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('destination',type=Path);a=p.parse_args();convert(a.source,a.destination)
