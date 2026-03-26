/**
 * Round-trip test: JPEG -> JXL -> JPEG
 *
 * Verifies that:
 * 1. jpegToJxl produces a smaller output than the input
 * 2. jxlToJpeg reconstructs the exact original JPEG (byte-identical)
 * 3. Error cases are handled correctly
 *
 * Run: node --test test/round-trip.test.js
 *
 * Note: Requires the WASM to be built first (npm run build:wasm)
 */

import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));

// Test fixtures -- place JPEG files in test/fixtures/
const FIXTURES_DIR = join(__dirname, 'fixtures');

async function loadFixture(name) {
  const buf = await readFile(join(FIXTURES_DIR, name));
  return buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength);
}

// Dynamic import so the test file can exist before the WASM is built
async function loadLib() {
  const lib = await import('../dist/index.js');
  return lib;
}

describe('jpeg-to-jxl round-trip', async () => {
  let jpegToJxl, jxlToJpeg;

  // Load library once
  try {
    const lib = await loadLib();
    jpegToJxl = lib.jpegToJxl;
    jxlToJpeg = lib.jxlToJpeg;
  } catch (e) {
    console.error('Could not load library. Is the WASM built? Run: npm run build');
    console.error(e.message);
    process.exit(1);
  }

  it('should produce smaller JXL from JPEG', async () => {
    const jpeg = await loadFixture('sample.jpg');
    const jxl = await jpegToJxl(jpeg);

    assert.ok(jxl instanceof ArrayBuffer, 'Output should be ArrayBuffer');
    assert.ok(jxl.byteLength > 0, 'Output should not be empty');
    assert.ok(jxl.byteLength < jpeg.byteLength,
      `JXL (${jxl.byteLength}) should be smaller than JPEG (${jpeg.byteLength})`);

    const savings = ((1 - jxl.byteLength / jpeg.byteLength) * 100).toFixed(1);
    console.log(`  Compression: ${jpeg.byteLength} -> ${jxl.byteLength} (${savings}% smaller)`);
  });

  it('should reconstruct byte-identical JPEG', async () => {
    const jpeg = await loadFixture('sample.jpg');
    const jxl = await jpegToJxl(jpeg);
    const reconstructed = await jxlToJpeg(jxl);

    assert.ok(reconstructed instanceof ArrayBuffer, 'Output should be ArrayBuffer');
    assert.equal(reconstructed.byteLength, jpeg.byteLength,
      'Reconstructed JPEG should be same size as original');

    // Byte-by-byte comparison
    const original = new Uint8Array(jpeg);
    const rebuilt = new Uint8Array(reconstructed);
    let firstDiff = -1;
    for (let i = 0; i < original.length; i++) {
      if (original[i] !== rebuilt[i]) {
        firstDiff = i;
        break;
      }
    }
    assert.equal(firstDiff, -1,
      firstDiff >= 0
        ? `Bytes differ at offset ${firstDiff}: original=0x${original[firstDiff].toString(16)} rebuilt=0x${rebuilt[firstDiff].toString(16)}`
        : 'Bytes are identical');
  });

  it('should accept Uint8Array input', async () => {
    const jpeg = await loadFixture('sample.jpg');
    const uint8 = new Uint8Array(jpeg);
    const jxl = await jpegToJxl(uint8);
    assert.ok(jxl.byteLength > 0);
  });

  it('should respect effort parameter', async () => {
    const jpeg = await loadFixture('sample.jpg');

    const fast = await jpegToJxl(jpeg, { effort: 1 });
    const slow = await jpegToJxl(jpeg, { effort: 9 });

    // Higher effort should produce smaller (or equal) output
    assert.ok(slow.byteLength <= fast.byteLength,
      `effort=9 (${slow.byteLength}) should be <= effort=1 (${fast.byteLength})`);

    console.log(`  effort=1: ${fast.byteLength}, effort=9: ${slow.byteLength}`);
  });

  it('should throw on invalid input', async () => {
    await assert.rejects(
      () => jpegToJxl(new ArrayBuffer(0)),
      { message: /not.*valid JPEG/i }
    );

    await assert.rejects(
      () => jpegToJxl(new Uint8Array([0x00, 0x00, 0x00])),
      { message: /not.*valid JPEG/i }
    );
  });

  it('should throw when JXL has no JPEG reconstruction data', async () => {
    // A minimal valid JXL that was NOT created from a JPEG
    // (if you have one in fixtures, use it; otherwise skip)
    try {
      const jxl = await loadFixture('non-jpeg-source.jxl');
      await assert.rejects(
        () => jxlToJpeg(jxl),
        { message: /reconstruction/i }
      );
    } catch (e) {
      if (e.code === 'ENOENT') {
        console.log('  (skipped: no non-jpeg-source.jxl fixture)');
        return;
      }
      throw e;
    }
  });
});
