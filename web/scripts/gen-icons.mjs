#!/usr/bin/env node
// Generates the PWA / iOS icons as PNG files with no dependencies beyond
// Node's built-in zlib: a tiny signed-distance-field renderer (4x4
// supersampling) plus a minimal PNG encoder.
//
//   node scripts/gen-icons.mjs        -> public/icons/*.png
//
// Motif: a sun over a battery gauge on a dark navy tile.
import { deflateSync } from 'node:zlib';
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const OUT = join(dirname(fileURLToPath(import.meta.url)), '..', 'public', 'icons');

// ---------- PNG encoder ----------
const CRC_TABLE = new Uint32Array(256).map((_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});
function crc32(buf) {
  let c = 0xffffffff;
  for (const b of buf) c = CRC_TABLE[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
}
function encodePng(w, h, rgba) {
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0);
  ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; // bit depth
  ihdr[9] = 6; // RGBA
  ihdr[10] = 0;
  ihdr[11] = 0;
  ihdr[12] = 0;
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (w * 4 + 1)] = 0; // filter: none
    rgba.copy(raw, y * (w * 4 + 1) + 1, y * w * 4, (y + 1) * w * 4);
  }
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(raw, { level: 9 })),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

// ---------- SDF scene (unit square, y down) ----------
const hex = (s) => [1, 3, 5].map((i) => parseInt(s.slice(i, i + 2), 16));
const C = {
  bgTop: hex('#16213a'),
  bgBottom: hex('#0b1020'),
  sun: hex('#ffb020'),
  ray: hex('#ffc94d'),
  shell: hex('#e8edf5'),
  fill: hex('#2fd27a'),
  gap: hex('#0f172a'),
};
const len = (x, y) => Math.hypot(x, y);
const sdCircle = (px, py, cx, cy, r) => len(px - cx, py - cy) - r;
function sdSegment(px, py, ax, ay, bx, by, r) {
  const pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
  const h = Math.max(0, Math.min(1, (pax * bax + pay * bay) / (bax * bax + bay * bay)));
  return len(pax - bax * h, pay - bay * h) - r;
}
function sdRoundBox(px, py, cx, cy, hw, hh, r) {
  const qx = Math.abs(px - cx) - hw + r, qy = Math.abs(py - cy) - hh + r;
  return len(Math.max(qx, 0), Math.max(qy, 0)) + Math.min(Math.max(qx, qy), 0) - r;
}

/** Colour at a point in content space (0..1). */
function background(y) {
  const t = Math.min(1, Math.max(0, y));
  return [0, 1, 2].map((k) => C.bgTop[k] + (C.bgBottom[k] - C.bgTop[k]) * t);
}

function scene(x, y, bgY = y) {
  let col = background(bgY);
  const sx = 0.5, sy = 0.37;
  // rays
  for (let i = 0; i < 8; i++) {
    const a = (i * Math.PI) / 4;
    const ax = sx + Math.cos(a) * 0.22, ay = sy + Math.sin(a) * 0.22;
    const bx = sx + Math.cos(a) * 0.28, by = sy + Math.sin(a) * 0.28;
    if (sdSegment(x, y, ax, ay, bx, by, 0.028) < 0) col = C.ray;
  }
  if (sdCircle(x, y, sx, sy, 0.155) < 0) col = C.sun;
  // battery: shell, inner gap, 3/4 fill, terminal nub
  const bx = 0.5, by = 0.81;
  if (sdRoundBox(x, y, 0.735, by, 0.02, 0.035, 0.01) < 0) col = C.shell;
  if (sdRoundBox(x, y, bx, by, 0.22, 0.075, 0.03) < 0) col = C.shell;
  if (sdRoundBox(x, y, bx, by, 0.195, 0.05, 0.015) < 0) col = C.gap;
  if (sdRoundBox(x, y, bx - 0.195 + 0.146, by, 0.13, 0.036, 0.01) < 0) col = C.fill;
  return col;
}

/** Render at size px. `inset` shrinks the motif (maskable safe zone = 80 %). */
function render(size, inset = 1) {
  const rgba = Buffer.alloc(size * size * 4);
  const SS = 4;
  for (let py = 0; py < size; py++) {
    for (let px = 0; px < size; px++) {
      let r = 0, g = 0, b = 0;
      for (let j = 0; j < SS; j++) {
        for (let i = 0; i < SS; i++) {
          const u = (px + (i + 0.5) / SS) / size;
          const v = (py + (j + 0.5) / SS) / size;
          const cu = 0.5 + (u - 0.5) / inset;
          const cv = 0.5 + (v - 0.5) / inset;
          // Outside the motif area (maskable padding) keep the background gradient.
          const c = cu < 0 || cu > 1 || cv < 0 || cv > 1 ? background(v) : scene(cu, cv, v);
          r += c[0]; g += c[1]; b += c[2];
        }
      }
      const o = (py * size + px) * 4;
      const n = SS * SS;
      rgba[o] = Math.round(r / n);
      rgba[o + 1] = Math.round(g / n);
      rgba[o + 2] = Math.round(b / n);
      rgba[o + 3] = 255;
    }
  }
  return encodePng(size, size, rgba);
}

mkdirSync(OUT, { recursive: true });
const targets = [
  ['icon-192.png', 192, 1],
  ['icon-512.png', 512, 1],
  ['icon-maskable-512.png', 512, 0.8],
  ['apple-touch-icon.png', 180, 1],
];
for (const [name, size, inset] of targets) {
  const png = render(size, inset);
  writeFileSync(join(OUT, name), png);
  console.log(`icons: ${name} ${size}x${size} ${png.length} bytes`);
}
