import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { SolcWireAdapter } from './solc-wire-adapter.mjs';

const [, , binaryPath, vectorPath] = process.argv;
if (!binaryPath || !vectorPath) {
  throw new Error('usage: node test-adapter.mjs <solc-bridge> <legacy.hex>');
}
const text = await readFile(vectorPath, 'utf8');
const hex = text
  .split('\n')
  .map((line) => line.split('#')[0])
  .join('')
  .replaceAll(/\s/g, '');
const transaction = Buffer.from(hex, 'hex').toString('base64');
const adapter = new SolcWireAdapter(binaryPath);
try {
  const inspected = await adapter.inspect(transaction);
  assert.equal(inspected.summary.version, 'legacy');
  assert.equal(inspected.summary.wireBytes, 174);
  const rpc = await adapter.rpcSendRequest(transaction);
  assert.equal(rpc.rpcRequest.method, 'sendTransaction');
  assert.equal(rpc.rpcRequest.params[0], transaction);
  await assert.rejects(adapter.inspect('Zh=='), (error) => error.code === 3);
} finally {
  await adapter.close();
}
console.log('Trebuchet process adapter: ok');
