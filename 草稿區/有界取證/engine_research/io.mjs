import fs from 'node:fs/promises';
import path from 'node:path';
import { createHash } from 'node:crypto';

export const sha = bytes => createHash('sha256').update(bytes).digest('hex').toUpperCase();
export const fileSha = async p => sha(await fs.readFile(p));
export const readJson = async p => JSON.parse(await fs.readFile(p, 'utf8'));
export const canonical = x => JSON.stringify(sort(x));
function sort(x) {
  if (Array.isArray(x)) return x.map(sort);
  if (x && typeof x === 'object') return Object.fromEntries(Object.keys(x).sort().map(k => [k, sort(x[k])]));
  return x;
}
export function absolute(p, label = 'path') {
  if (typeof p !== 'string' || !path.isAbsolute(p)) throw new Error(`${label} must be absolute`);
  return path.resolve(p);
}
export function duration(n, label) {
  if (!Number.isSafeInteger(n) || n < 1 || n > 2147483647) throw new Error(`${label} must be a positive bounded integer`);
  return n;
}
export async function writeNew(dir, name, value) {
  if (path.basename(name) !== name) throw new Error('Output filename must be local');
  const target = path.join(dir, name);
  const tmp = target + '.writing';
  await fs.writeFile(tmp, JSON.stringify(value, null, 2) + '\n', { flag: 'wx' });
  // link is an atomic no-replace publish, unlike rename on platforms that replace.
  await fs.link(tmp, target);
  await fs.unlink(tmp);
}
export function field(value, parts) {
  if (!Array.isArray(parts) || parts.some(p => !['string', 'number'].includes(typeof p) || ['__proto__', 'prototype', 'constructor'].includes(String(p))))
    throw new Error('Invalid field path');
  for (const p of parts) {
    if (value === null || typeof value !== 'object' || !Object.hasOwn(value, p)) throw new Error(`Missing field: ${parts.join('.')}`);
    value = value[p];
  }
  return value;
}
export const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
