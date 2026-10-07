import {createRequire} from 'node:module';
import {resolve} from 'node:path';
import {createInterface} from 'node:readline';
const require=createRequire(import.meta.url);
const factory=require(resolve(process.argv[2]));
const module=await factory({noInitialRun:true,print: text=>process.stderr.write(text+'\n'),printErr:text=>process.stderr.write(text+'\n')});
try {
  for await(const line of createInterface({input:process.stdin,crlfDelay:Infinity})) {
    const pointer=module.ccall('S3Control','number',['string'],[line]);
    process.stdout.write(module.UTF8ToString(pointer)+'\n');
  }
} finally {
  // EOF owns cancellation/VM retirement even when the invocation is paused.
  const pointer=module.ccall('S3Control','number',['string'],['{"version":1,"action":"close"}']);
  const retired=JSON.parse(module.UTF8ToString(pointer));
  if(!retired.ok||retired.heap!==0)throw Error('Wasm VM retirement failed');
}
