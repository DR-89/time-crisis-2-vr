"""Bake the single-mesh Tripo GLB into the small, validated native gun format.

GLB source remains in artifacts; the runtime asset has interleaved position,
normal and UV floats, uint32 triangles and a 1024px RGBA albedo. No GLB parser
or image decoder is needed on the Quest. Coordinates: metres, +Y up, -Z aim.
"""
from pathlib import Path
import argparse, hashlib, io, json, struct
import numpy as np
from PIL import Image

def convert(source, destination, front, barrel_y, length=.23, rear=.03, task_id=None):
    assert np.isfinite([barrel_y,length,rear]).all() and .1<=length<=1.2 and 0<=rear<length,'Invalid physical dimensions'
    raw=source.read_bytes()
    assert struct.unpack_from('<III',raw)==(0x46546C67,2,len(raw)),'Invalid GLB'
    offset=12;document=None;binary=None
    while offset<len(raw):
        chunk_length,kind=struct.unpack_from('<II',raw,offset);offset+=8
        chunk=raw[offset:offset+chunk_length];assert len(chunk)==chunk_length;offset+=chunk_length
        if kind==0x4E4F534A:document=json.loads(chunk)
        elif kind==0x004E4942:binary=chunk
    assert document and binary and not document.get('extensionsRequired')
    assert len(document['meshes'])==1 and len(document['nodes'])==1
    node=document['nodes'][0];assert not any(k in node for k in ('matrix','rotation','scale','translation')),'Bake node transform first'
    primitives=document['meshes'][0]['primitives'];assert len(primitives)==1
    primitive=primitives[0];assert primitive.get('mode',4)==4
    def accessor(index):
        a=document['accessors'][index];assert not a.get('sparse') and not a.get('normalized')
        v=document['bufferViews'][a['bufferView']];assert v.get('buffer',0)==0
        dtype=np.dtype({5123:'<u2',5125:'<u4',5126:'<f4'}[a['componentType']]);width={'SCALAR':1,'VEC2':2,'VEC3':3}[a['type']]
        start=v.get('byteOffset',0)+a.get('byteOffset',0);stride=v.get('byteStride',width*dtype.itemsize)
        return np.ndarray((a['count'],width),dtype,buffer=binary,offset=start,strides=(stride,dtype.itemsize)).copy()
    attrs=primitive['attributes'];pos=accessor(attrs['POSITION']);normal=accessor(attrs['NORMAL']);uv=accessor(attrs['TEXCOORD_0']);indices=accessor(primitive['indices']).reshape(-1)
    assert len(pos)==len(normal)==len(uv) and indices.max()<len(pos) and len(indices)%3==0
    original_bounds=[pos.min(0).tolist(),pos.max(0).tolist()]
    # Tripo puts the barrel on the Z axis. A half-turn keeps winding/handedness.
    if front=='+z':pos[:,[0,2]]*=-1;normal[:,[0,2]]*=-1
    extent=pos.max(0)-pos.min(0);assert extent[2]>1e-6,'Model has no barrel-axis extent'
    scale=length/extent[2]
    pos[:,0]-=(pos[:,0].min()+pos[:,0].max())*.5
    pos[:,1]-=barrel_y
    pos[:,2]-=pos[:,2].max()
    pos*=scale
    # Physical length and rear overhang locate the grip relative to the aim
    # pose. A machine gun must not be squeezed into pistol dimensions.
    pos[:,2]+=rear
    normal/=np.maximum(np.linalg.norm(normal,axis=1,keepdims=True),1e-8)
    material=document['materials'][primitive['material']]['pbrMetallicRoughness']
    assert material.get('baseColorFactor',[1,1,1,1])==[1,1,1,1]
    image_index=document['textures'][material['baseColorTexture']['index']]['source']
    image=document['images'][image_index];view=document['bufferViews'][image['bufferView']]
    start=view.get('byteOffset',0);albedo=Image.open(io.BytesIO(binary[start:start+view['byteLength']])).convert('RGBA')
    albedo.thumbnail((1024,1024),Image.Resampling.LANCZOS)
    vertices=np.concatenate([pos,normal,uv],axis=1).astype('<f4');indices=indices.astype('<u4')
    assert np.isfinite(vertices).all() and 0<len(pos)<=100000 and 0<len(indices)<=300000
    tip=[0.,0.,float(pos[:,2].min())]
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_bytes(struct.pack('<8sIIII3f',b'TCGUN001',len(pos),len(indices),*albedo.size,*tip)+vertices.tobytes()+indices.tobytes()+albedo.tobytes())
    report={'generator':'Tripo3D','source_sha256':hashlib.sha256(raw).hexdigest(),'asset_sha256':hashlib.sha256(destination.read_bytes()).hexdigest(),'triangles':len(indices)//3,'vertices':len(pos),'albedo_size':list(albedo.size),'source_front':front,'source_barrel_y':barrel_y,'length_m':length,'rear_overhang_m':rear,'source_bounds':original_bounds,'bounds_m':[pos.min(0).tolist(),pos.max(0).tolist()],'muzzle_m':tip,'material':'base color with mesh normals and lightweight directional lighting; source PBR maps retained in GLB'}
    if task_id:report['task_id']=task_id
    destination.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('destination',type=Path);p.add_argument('--front',choices=['+z','-z'],default='+z');p.add_argument('--barrel-y',type=float,default=.24)
    p.add_argument('--length',type=float,default=.23);p.add_argument('--rear',type=float,default=.03);p.add_argument('--task-id');a=p.parse_args()
    convert(a.source,a.destination,a.front,a.barrel_y,a.length,a.rear,a.task_id)
