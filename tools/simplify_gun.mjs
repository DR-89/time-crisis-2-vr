// Offline asset tool only. Install the pinned WASM dependency with:
// npm install --prefix .tools/meshoptimizer --ignore-scripts meshoptimizer@1.3.0
import fs from 'node:fs';
import { MeshoptSimplifier } from '../.tools/meshoptimizer/node_modules/meshoptimizer/index.js';
await MeshoptSimplifier.ready;
const job=JSON.parse(fs.readFileSync(process.argv[2],'utf8'));
const results=job.parts.map(p=>{
  const positions=new Float32Array(p.positions),attributes=new Float32Array(p.attributes);
  const indices=new Uint32Array(p.indices);
  const [result,error]=MeshoptSimplifier.simplifyWithAttributes(
    indices,positions,3,attributes,5,[.15,.15,.15,.15,.15],null,
    Math.min(indices.length,p.target*3),.02,['Permissive']);
  return {indices:Array.from(result),error};
});
fs.writeFileSync(process.argv[3],JSON.stringify(results));
