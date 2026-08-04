import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildCommand, chunk, toBase64 } from './framing.js';

test('toBase64 encodes ASCII', () => {
  assert.equal(toBase64('hello'), 'aGVsbG8=');
});

test('toBase64 encodes UTF-8 multi-byte input', () => {
  // The Flipper will reject this with ERR unmappable, but encoding must
  // still be byte-correct rather than throwing.
  assert.equal(toBase64('é'), 'w6k=');
});

test('buildCommand produces a CR-terminated kmtype line', () => {
  assert.equal(buildCommand('hello'), 'kmtype aGVsbG8=\r');
});

test('chunk splits to the given size', () => {
  const bytes = new Uint8Array([1, 2, 3, 4, 5]);
  const parts = chunk(bytes, 2);
  assert.equal(parts.length, 3);
  assert.deepEqual([...parts[0]], [1, 2]);
  assert.deepEqual([...parts[2]], [5]);
});

test('chunk returns one part when input fits', () => {
  const parts = chunk(new Uint8Array([1, 2]), 20);
  assert.equal(parts.length, 1);
});

test('chunk returns nothing for empty input', () => {
  assert.equal(chunk(new Uint8Array([]), 20).length, 0);
});
