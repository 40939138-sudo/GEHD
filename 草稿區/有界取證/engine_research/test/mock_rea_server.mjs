#!/usr/bin/env node
// Synthetic MCP qualification fixture. This does not run REA, Ghidra or a game.
// GE_MOCK_SCENARIO: success | tool_refusal | delay_ignore_cancel | close_error |
// disconnect. Optional suffix :tool_name chooses the affected tools/call.
// GE_MOCK_DELAY_MS: integer 0..30000 (default 500). GE_MOCK_LOG: absolute JSONL.
// Flat fixture mapping only: addresses >= 0x401000 map to file offset address-base;
// smaller addresses are file offsets. Assembly is invented, never disassembled.
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
import { createHash } from 'node:crypto';

const BASE = 0x401000;
const MAX_TARGET_BYTES = 64 * 1024 * 1024;
const MAX_INPUT_LINE = 1024 * 1024;
const MAX_REQUESTS = 256;
const MAX_LIFETIME_MS = 120000;
const started = performance.now();
const specification = process.env.GE_MOCK_SCENARIO || 'success';
const [scenario, selectedTool, extra] = specification.split(':');
const scenarios = new Set(['success', 'tool_refusal', 'delay_ignore_cancel', 'close_error', 'disconnect']);
const delayMs = Number(process.env.GE_MOCK_DELAY_MS ?? 500);
const logPath = process.env.GE_MOCK_LOG;
const affectedTool = selectedTool || (scenario === 'close_error' ? 'close_binary' : 'analyze_function');
let sequence = 0;
let stopped = false;
let initialized = false;
let target = null;
let requestCount = 0;
let pending = '';
const timers = new Set();
const records = [];
const activeRequests = new Map();
const provider = { id: 'ge-synthetic-mock', name: 'Synthetic qualification fixture', version: '1.0.0' };
const limitation = 'SYNTHETIC FIXTURE ONLY: no REA/Ghidra execution, no PE mapping or GE mechanism evidence.';
const digest = bytes => createHash('sha256').update(bytes).digest('hex');

function log(kind, details = {}) {
  if (!logPath) return;
  fs.appendFileSync(logPath, JSON.stringify({
    sequence: sequence++, kind, synthetic: true, pid: process.pid,
    elapsedMs: Math.round(performance.now() - started), ...details
  }) + '\n');
}

function shutdown(code, reason) {
  if (stopped) return;
  stopped = true;
  for (const timer of timers) clearTimeout(timer);
  timers.clear();
  clearTimeout(watchdog);
  try { log('shutdown', { code, reason, activeRequestIds: [...activeRequests.keys()] }); } catch {}
  process.stdin.pause();
  // Flush protocol output on EOF; the bound also handles a broken output pipe.
  const fallback = setTimeout(() => process.exit(code), 1000);
  fallback.unref();
  process.stdout.end(() => process.exit(code));
}

function send(message) {
  if (stopped) return;
  log('response', { message });
  process.stdout.write(JSON.stringify(message) + '\n');
}

function rpcError(id, code, message) {
  send({ jsonrpc: '2.0', id, error: { code, message } });
}

function toolResult(data, isError = false) {
  return {
    _meta: { synthetic: true, provenance: { source: 'synthetic_fixture', untrusted_content: true } },
    content: [{ type: 'text', text: JSON.stringify(data) }],
    structuredContent: data,
    ...(isError ? { isError: true } : {})
  };
}

function toolError(name, message, category = 'invalid_input') {
  return toolResult({ synthetic: true, error: {
    code: 'invalid_request', category, message,
    // Deliberately misleading: callers must classify invalid_input before retrying.
    retryable: category === 'invalid_input',
    remediation: { action: 'Inspect the synthetic test case; do not retry unchanged invalid input.' },
    details: { operation: name, synthetic: true }
  } }, true);
}

function addressValue(value) {
  if (typeof value !== 'string' || !/^(?:0x[0-9a-f]+|[0-9]+)$/i.test(value)) {
    throw new Error('Fixture address must be an explicit hexadecimal or decimal string.');
  }
  const n = Number(value);
  if (!Number.isSafeInteger(n) || n < 0) throw new Error('Fixture address is out of range.');
  return n;
}

function evidence(operation, parameters, normalized) {
  const subject = {
    name: path.basename(target.path), digest: { sha256: target.sha256 },
    local_path: target.path, format: 'synthetic-flat', architecture: 'x86', synthetic: true
  };
  const profileParameters = { synthetic: true, flat_base: BASE };
  const record = {
    synthetic: true,
    evidence_id: 'ev_synthetic_' + digest(JSON.stringify({ subject, operation, parameters, normalized })),
    subject, provider,
    analysis_profile: { provider, parameters: profileParameters, digest: digest(JSON.stringify(profileParameters)) },
    predicate_type: 'rea.analysis', operation, parameters,
    raw_result: { value: normalized, limitations: [limitation] },
    normalized_result: normalized, confidence: 'synthetic', authority: 'synthetic-fixture',
    limitations: [limitation], locations: [], evidence_links: []
  };
  records.push(record);
  return toolResult(record);
}

const schema = (properties = {}, required = []) => ({ type: 'object', properties, required, additionalProperties: false });
const string = { type: 'string' };
const tools = [
  { name: 'open_binary', inputSchema: schema({ path: string, provider_id: string }, ['path']) },
  { name: 'analyze_function', inputSchema: schema({ procedure: string }, ['procedure']) },
  { name: 'procedure_assembly', inputSchema: schema({ procedure: string }, ['procedure']) },
  { name: 'read_bytes', inputSchema: schema({ address: string, length: { type: 'integer', minimum: 0, maximum: 65536 } }, ['address', 'length']) },
  { name: 'export_evidence_bundle', inputSchema: schema({ path: string, overwrite: { type: 'boolean' } }, ['path']) },
  { name: 'close_binary', inputSchema: schema() },
  { name: 'binary_session', inputSchema: schema() }
].map(tool => ({ ...tool, description: `SYNTHETIC qualification fixture: ${tool.name}. Not REA.` }));

async function callTool(name, args) {
  if (!tools.some(tool => tool.name === name)) return toolError(name, 'Unknown fixture tool.');
  if (name === 'binary_session') {
    return toolResult({ result: { synthetic: true, state: target ? 'open' : 'closed', target, activeRequests: [...activeRequests.keys()] } });
  }
  if (name === 'open_binary') {
    if (target) return toolError(name, 'A synthetic target is already open.');
    if (typeof args.path !== 'string' || !path.isAbsolute(args.path)) return toolError(name, 'An absolute fixture path is required.');
    const stat = await fsp.stat(args.path);
    if (!stat.isFile() || stat.size > MAX_TARGET_BYTES) return toolError(name, 'Fixture must be a file of at most 64 MiB.');
    const bytes = await fsp.readFile(args.path);
    target = { path: path.resolve(args.path), sha256: digest(bytes), format: 'synthetic-flat', kind: 'fixture', architecture: 'x86', synthetic: true };
    return toolResult({ result: { ...target } });
  }
  if (name === 'close_binary') {
    target = null;
    return toolResult({ result: null, synthetic: true });
  }
  if (name === 'export_evidence_bundle') {
    if (typeof args.path !== 'string' || !path.isAbsolute(args.path)) return toolError(name, 'An absolute output path is required.');
    if (target && path.resolve(args.path).toLowerCase() === target.path.toLowerCase()) return toolError(name, 'Cannot export onto the target.');
    const bundle = { synthetic: true, limitations: [limitation], artifacts: [], captures: [], environments: [], providers: [provider], records, scenarios: [], unknowns: [] };
    const text = JSON.stringify(bundle, null, 2) + '\n';
    // Exclusive creation: even overwrite=true never replaces pre-existing evidence.
    await fsp.writeFile(args.path, text, { flag: 'wx' });
    return toolResult({ result: { path: path.resolve(args.path), bytes: Buffer.byteLength(text), records: records.length, unknowns: 0, synthetic: true } });
  }
  if (!target) return toolError(name, 'No synthetic target is open.');
  if (name === 'read_bytes') {
    const address = addressValue(args.address);
    const length = args.length;
    if (!Number.isInteger(length) || length < 0 || length > 65536) return toolError(name, 'length must be an integer in 0..65536.');
    const offset = address >= BASE ? address - BASE : address;
    const file = await fsp.open(target.path, 'r');
    try {
      const buffer = Buffer.alloc(length);
      const { bytesRead } = await file.read(buffer, 0, length, offset);
      if (bytesRead !== length) return toolError(name, 'Requested synthetic flat range exceeds fixture bytes.');
      return evidence(name, args, { address: '0x' + address.toString(16), length, bytes: [...buffer], hex: buffer.toString('hex'), synthetic: true });
    } finally { await file.close(); }
  }
  const address = addressValue(args.procedure);
  const hex = '0x' + address.toString(16);
  return evidence(name, args, {
    procedure: { address: hex, name: 'SYNTHETIC_FUNCTION', classification: { external: false, thunk: false }, synthetic: true },
    assembly: [`${hex}: NOP`, `0x${(address + 1).toString(16)}: RET`],
    ...(name === 'analyze_function' ? { pseudocode: '/* SYNTHETIC FIXTURE */ void mock_function(void) {}' } : {}),
    callers: [], callees: [], unresolved_calls: [],
    facet_availability: { assembly: { available: true }, pseudocode: { available: name === 'analyze_function' } },
    limitations: [limitation], synthetic: true
  });
}

async function handle(message) {
  const hasId = Object.hasOwn(message, 'id');
  log(message.method === 'notifications/cancelled' ? 'cancel' : 'request', { message });
  if (message.method === 'notifications/cancelled') {
    log('cancel_ignored', { requestId: message.params?.requestId, active: activeRequests.has(message.params?.requestId) });
    return; // This fixture deliberately leaves the matching timer running.
  }
  if (!hasId) return;
  if (message.method === 'initialize') {
    initialized = true;
    return send({ jsonrpc: '2.0', id: message.id, result: {
      protocolVersion: '2025-11-25', capabilities: { tools: {} },
      serverInfo: { name: 'ge-synthetic-mock-rea', version: '1.0.0' },
      instructions: limitation
    } });
  }
  if (message.method === 'ping') return send({ jsonrpc: '2.0', id: message.id, result: {} });
  if (!initialized) return rpcError(message.id, -32000, 'Initialize the synthetic fixture first.');
  if (message.method === 'tools/list') return send({ jsonrpc: '2.0', id: message.id, result: { tools } });
  if (message.method !== 'tools/call') return rpcError(message.id, -32601, 'Unsupported synthetic fixture method.');
  const name = message.params?.name;
  const args = message.params?.arguments ?? {};
  activeRequests.set(message.id, name);
  const affected = name === affectedTool;
  if (affected && scenario === 'disconnect') return shutdown(73, 'synthetic_disconnect_after_request');
  const complete = async () => {
    try {
      let result;
      if (affected && scenario === 'tool_refusal') result = toolError(name, 'Synthetic procedure is not defined at this address.');
      else if (affected && scenario === 'close_error') result = toolError(name, 'Synthetic close failure: resource remains owned.', 'backend_failure');
      else result = await callTool(name, args);
      send({ jsonrpc: '2.0', id: message.id, result });
    } catch (error) {
      if (!stopped) send({ jsonrpc: '2.0', id: message.id, result: toolError(name, `Synthetic fixture error: ${error.message}`) });
    } finally { activeRequests.delete(message.id); }
  };
  if (affected && scenario === 'delay_ignore_cancel') {
    log('delay_started', { requestId: message.id, tool: name, delayMs });
    const timer = setTimeout(() => { timers.delete(timer); void complete().catch(fatal); }, delayMs);
    timers.add(timer);
  } else await complete();
}

function fatal(error) {
  process.stderr.write(`Synthetic MCP fixture: ${error.message}\n`);
  shutdown(70, 'fixture_error');
}

const watchdog = setTimeout(() => shutdown(74, 'synthetic_lifetime_limit'), MAX_LIFETIME_MS);
try {
  if (!scenarios.has(scenario) || extra || (selectedTool && !tools.some(tool => tool.name === selectedTool))) throw new Error('Invalid GE_MOCK_SCENARIO.');
  if (!Number.isInteger(delayMs) || delayMs < 0 || delayMs > 30000) throw new Error('GE_MOCK_DELAY_MS must be an integer in 0..30000.');
  if (logPath) {
    if (!path.isAbsolute(logPath)) throw new Error('GE_MOCK_LOG must be absolute.');
    fs.mkdirSync(path.dirname(logPath), { recursive: true });
  }
  log('startup', { scenario, affectedTool, delayMs, protocolVersion: '2025-11-25', limitations: [limitation] });
} catch (error) { fatal(error); }

process.stdout.on('error', error => shutdown(error.code === 'EPIPE' ? 0 : 70, 'stdout_error'));
process.stdin.on('error', error => fatal(error));
process.stdin.setEncoding('utf8');
process.stdin.on('data', chunk => {
  if (stopped) return;
  pending += chunk;
  let newline;
  while ((newline = pending.indexOf('\n')) !== -1) {
    const line = pending.slice(0, newline).replace(/\r$/, '');
    pending = pending.slice(newline + 1);
    if (!line.trim()) continue;
    if (line.length > MAX_INPUT_LINE || ++requestCount > MAX_REQUESTS) return shutdown(75, 'synthetic_input_limit');
    let message;
    try { message = JSON.parse(line); } catch { rpcError(null, -32700, 'Invalid JSON.'); continue; }
    if (!message || Array.isArray(message) || message.jsonrpc !== '2.0' || typeof message.method !== 'string') {
      rpcError(message?.id ?? null, -32600, 'Invalid JSON-RPC request.');
      continue;
    }
    void handle(message).catch(fatal);
  }
  if (pending.length > MAX_INPUT_LINE) shutdown(75, 'synthetic_input_limit');
});
process.stdin.on('end', () => shutdown(0, 'stdin_eof'));
process.on('SIGINT', () => shutdown(0, 'SIGINT'));
process.on('SIGTERM', () => shutdown(0, 'SIGTERM'));
