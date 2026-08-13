import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';

const REQUEST_SCHEMA = 'solc-process-request/v1';
const RESPONSE_SCHEMA = 'solc-process-response/v1';
const MAX_RESPONSE_BYTES = 64 * 1024;

export class SolcWireAdapter {
  constructor(binaryPath) {
    if (typeof binaryPath !== 'string' || binaryPath.length === 0) {
      throw new TypeError('An explicit solc-bridge binary path is required.');
    }
    this.nextId = 1;
    this.pending = new Map();
    this.child = spawn(binaryPath, [], {
      stdio: ['pipe', 'pipe', 'pipe'],
      windowsHide: true,
      shell: false,
    });
    this.stderr = '';
    this.child.stderr.setEncoding('utf8');
    this.child.stderr.on('data', (chunk) => {
      this.stderr = `${this.stderr}${chunk}`.slice(-4096);
    });
    createInterface({ input: this.child.stdout, crlfDelay: Infinity }).on(
      'line',
      (line) => this.#receive(line),
    );
    this.child.once('error', (error) => this.#failAll(error));
    this.child.once('exit', (code, signal) => {
      this.#failAll(
        new Error(
          `solc-bridge exited (${signal || (code ?? 'unknown')}): ${this.stderr.trim()}`,
        ),
      );
    });
  }

  #receive(line) {
    if (Buffer.byteLength(line, 'utf8') > MAX_RESPONSE_BYTES) {
      this.#failAll(new Error('solc-bridge response exceeded the adapter limit.'));
      this.child.kill();
      return;
    }
    let response;
    try {
      response = JSON.parse(line);
    } catch {
      this.#failAll(new Error('solc-bridge emitted invalid JSON.'));
      this.child.kill();
      return;
    }
    const pending = this.pending.get(response.id);
    if (!pending) return;
    this.pending.delete(response.id);
    if (response.schema !== RESPONSE_SCHEMA) {
      pending.reject(new Error(`Unsupported solc-bridge schema: ${response.schema}`));
    } else if (!response.ok) {
      const error = new Error(response.error?.message || 'solc-bridge rejected the request.');
      error.code = response.error?.code;
      error.offset = response.error?.offset;
      pending.reject(error);
    } else {
      pending.resolve(response.result);
    }
  }

  #failAll(error) {
    for (const pending of this.pending.values()) pending.reject(error);
    this.pending.clear();
  }

  request(operation, transactionBase64) {
    if (!this.child.stdin.writable) {
      return Promise.reject(new Error('solc-bridge stdin is closed.'));
    }
    const id = this.nextId++;
    const line = JSON.stringify({
      schema: REQUEST_SCHEMA,
      id,
      operation,
      encoding: 'base64',
      transaction: transactionBase64,
    });
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.child.stdin.write(`${line}\n`, (error) => {
        if (error && this.pending.delete(id)) reject(error);
      });
    });
  }

  inspect(transactionBase64) {
    return this.request('inspect', transactionBase64);
  }

  verify(transactionBase64) {
    return this.request('verify', transactionBase64);
  }

  rpcSendRequest(transactionBase64) {
    return this.request('rpc-send-request', transactionBase64);
  }

  async close() {
    if (this.child.exitCode !== null) return;
    this.child.stdin.end();
    await new Promise((resolve) => this.child.once('exit', resolve));
  }
}
