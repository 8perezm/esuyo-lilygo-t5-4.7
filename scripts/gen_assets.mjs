#!/usr/bin/env node
/**
 * Asset pipeline for the T5 dashboard firmware.
 *
 * Generates LVGL 9 C sources from upstream vector assets:
 *
 *   1. Roboto (Apache-2.0) TTFs  -> 4 bpp LVGL fonts      -> main/assets/fonts.c / fonts.h
 *   2. Lucide (ISC) SVG icons    -> 8 bpp alpha A8 images -> main/assets/icons.c / icons.h
 *
 * Raw sources are cached (and gitignored) in:
 *   assets/fonts/*.ttf
 *   assets/icons/*.svg
 *
 * The script is fully re-runnable: downloads are cached, lv_font_conv output is
 * normalised (no paths/timestamps), and files on disk are only rewritten when
 * their contents actually change. Running it twice produces identical bytes.
 *
 * Usage:
 *   node scripts/gen_assets.mjs           # fetch + generate + validate
 *   node scripts/gen_assets.mjs --clean   # delete generated C sources
 */

import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { Resvg } from '@resvg/resvg-js';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '..');

const FONT_DIR = path.join(ROOT, 'assets', 'fonts');
const ICON_DIR = path.join(ROOT, 'assets', 'icons');
const OUT_DIR = path.join(ROOT, 'main', 'assets');

const FONTS_C = path.join(OUT_DIR, 'fonts.c');
const FONTS_H = path.join(OUT_DIR, 'fonts.h');
const ICONS_C = path.join(OUT_DIR, 'icons.c');
const ICONS_H = path.join(OUT_DIR, 'icons.h');

/* ------------------------------------------------------------------ */
/*  Configuration                                                      */
/* ------------------------------------------------------------------ */

/**
 * Roboto hinted static builds (googlefonts/roboto-2, Apache-2.0).
 *
 * The npm packages `roboto-fontface` and `@fontsource/roboto` only ship WOFF /
 * WOFF2, and lv_font_conv rasterises through opentype.js which cannot decode
 * either, so the hinted TTFs from the upstream googlefonts/roboto-2 repository
 * are used directly. Every downloaded file is checked (sfnt signature + `name`
 * table) to be Roboto of the requested weight before it is accepted.
 */
const FONT_URL_BASE = 'https://raw.githubusercontent.com/googlefonts/roboto-2/main/src/hinted';

/**
 * Exact glyph set requested by the spec.
 *
 * 0xB7 (MIDDLE DOT) separates a city name from its region in the weather
 * header and labels the joke category; 0x2014 (EM DASH) prefixes a quote's
 * author. Without them LVGL draws a missing-glyph box on the panel.
 */
const GLYPH_RANGES = '0x20-0x7F,0xA3,0xB0,0xB7,0x20AC,0x2013,0x2014,0x2026,0x2191,0x2193';

/**
 * Fonts to build. `weight` selects the source TTF, `size` is the pixel size
 * passed to lv_font_conv, `symbol` is the C identifier of the lv_font_t.
 */
const FONT_SPECS = [
  { symbol: 'font_roboto_14', weight: 'Regular', size: 14 },
  { symbol: 'font_roboto_16', weight: 'Regular', size: 16 },
  { symbol: 'font_roboto_18', weight: 'Regular', size: 18 },
  { symbol: 'font_roboto_20', weight: 'Regular', size: 20 },
  { symbol: 'font_roboto_medium_18', weight: 'Medium', size: 18 },
  { symbol: 'font_roboto_medium_20', weight: 'Medium', size: 20 },
  { symbol: 'font_roboto_medium_22', weight: 'Medium', size: 22 },
  { symbol: 'font_roboto_medium_24', weight: 'Medium', size: 24 },
  { symbol: 'font_roboto_medium_28', weight: 'Medium', size: 28 },
  { symbol: 'font_roboto_bold_20', weight: 'Bold', size: 20 },
  { symbol: 'font_roboto_bold_24', weight: 'Bold', size: 24 },
  { symbol: 'font_roboto_bold_28', weight: 'Bold', size: 28 },
  { symbol: 'font_roboto_bold_36', weight: 'Bold', size: 36 },
  { symbol: 'font_roboto_bold_48', weight: 'Bold', size: 48 },
  { symbol: 'font_roboto_bold_96', weight: 'Bold', size: 96 },
];

/** Lucide icons (ISC), as requested. */
const ICONS = [
  'sun', 'cloud-sun', 'cloud', 'cloud-rain', 'cloud-snow', 'cloud-lightning',
  'cloud-fog', 'wind', 'droplet', 'thermometer', 'gauge', 'bitcoin',
  'trending-up', 'trending-down', 'chart-line', 'chart-candlestick', 'gem',
  'fuel', 'flame', 'zap', 'wifi', 'wifi-off', 'battery-medium', 'refresh-cw',
  'settings', 'moon', 'chevron-left', 'chevron-right', 'arrow-up', 'arrow-down',
  'map-pin', 'clock', 'alert-triangle', 'coins', 'circle-dollar-sign',
];

/** Fallback slugs for icons that Lucide renamed at some point. */
const ICON_ALIASES = {
  'alert-triangle': ['triangle-alert'],
  'chart-candlestick': ['chart-candlestick', 'candlestick-chart'],
  'circle-dollar-sign': ['circle-dollar-sign', 'badge-dollar-sign'],
  'chart-line': ['chart-line', 'line-chart'],
  'trending-up': ['trending-up', 'trending-up'],
};

/** Stroke width forced on the SVG root before rasterising (e-paper needs it). */
const ICON_STROKE_WIDTH = 2.2;

/** Rasterised icon sizes (px) and the `icon_get()` switch point. */
const ICON_SIZES = [24, 48];
const ICON_LARGE_THRESHOLD = 32;

/* ------------------------------------------------------------------ */
/*  Small helpers                                                      */
/* ------------------------------------------------------------------ */

const log = (...a) => console.log(...a);
const step = (tag, msg) => console.log(`[${tag}] ${msg}`);

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

/** Write only when the payload differs; returns true when the file changed. */
function writeIfChanged(file, content) {
  const buf = Buffer.isBuffer(content) ? content : Buffer.from(content, 'utf8');
  if (fs.existsSync(file)) {
    const prev = fs.readFileSync(file);
    if (prev.equals(buf)) return false;
  }
  fs.writeFileSync(file, buf);
  return true;
}

function fmtBytes(n) {
  if (n >= 1024 * 1024) return `${(n / (1024 * 1024)).toFixed(1)} MiB`;
  if (n >= 1024) return `${(n / 1024).toFixed(1)} KiB`;
  return `${n} B`;
}

function nonEmptyFile(file, minSize = 1) {
  try {
    const st = fs.statSync(file);
    return st.isFile() && st.size >= minSize;
  } catch {
    return false;
  }
}

async function download(url, label) {
  const res = await fetch(url, { redirect: 'follow' });
  if (!res.ok) throw new Error(`HTTP ${res.status} for ${url} (${label})`);
  return Buffer.from(await res.arrayBuffer());
}

/* ------------------------------------------------------------------ */
/*  TTF inspection (guards against a non-Roboto substitution)          */
/* ------------------------------------------------------------------ */

/**
 * Minimal SFNT `name` table reader. Returns the human readable names so the
 * pipeline can assert that the file really is Roboto of the expected weight.
 */
function readFontNames(buf) {
  const tag0 = buf.toString('latin1', 0, 4);
  const sfnt = buf.readUInt32BE(0);
  const isSfnt = sfnt === 0x00010000 || tag0 === 'OTTO' || tag0 === 'true' || tag0 === 'ttcf';
  if (!isSfnt) throw new Error('not a TrueType/OpenType file (bad sfnt signature)');

  const numTables = buf.readUInt16BE(4);
  let nameOffset = -1;
  for (let i = 0; i < numTables; i++) {
    const rec = 12 + i * 16;
    if (rec + 16 > buf.length) break;
    if (buf.toString('latin1', rec, rec + 4) === 'name') {
      nameOffset = buf.readUInt32BE(rec + 8);
      break;
    }
  }
  if (nameOffset < 0 || nameOffset + 6 > buf.length) return {};

  const count = buf.readUInt16BE(nameOffset + 2);
  const stringOffset = buf.readUInt16BE(nameOffset + 4);
  const wanted = new Set([1, 2, 4, 16, 17]);
  const byId = new Map();

  for (let i = 0; i < count; i++) {
    const rec = nameOffset + 6 + i * 12;
    if (rec + 12 > buf.length) break;
    const platformID = buf.readUInt16BE(rec);
    const nameID = buf.readUInt16BE(rec + 6);
    if (!wanted.has(nameID)) continue;

    const length = buf.readUInt16BE(rec + 8);
    const offset = buf.readUInt16BE(rec + 10);
    const start = nameOffset + stringOffset + offset;
    if (start + length > buf.length) continue;

    let value = '';
    if (platformID === 3 || platformID === 0) {
      for (let j = start; j + 1 < start + length; j += 2) {
        value += String.fromCharCode(buf.readUInt16BE(j));
      }
    } else {
      value = buf.toString('latin1', start, start + length);
    }
    value = value.replace(/\0/g, '').trim();
    // Prefer Windows/Unicode records, keep the first one per nameID.
    if (!byId.has(nameID) || platformID === 3) byId.set(nameID, value);
  }

  return {
    family: byId.get(1) || '',
    subfamily: byId.get(2) || '',
    fullName: byId.get(4) || '',
    typographicFamily: byId.get(16) || '',
    typographicSubfamily: byId.get(17) || '',
  };
}

/** Assert the file is Roboto, and the expected weight when it is unambiguous. */
function assertRoboto(file, buf, weight) {
  const names = readFontNames(buf);
  const blob = [names.family, names.typographicFamily, names.fullName].join(' ').toLowerCase();
  if (!blob.includes('roboto')) {
    throw new Error(
      `${path.basename(file)} is not Roboto (family="${names.family}", full="${names.fullName}")`,
    );
  }
  if (weight !== 'Regular') {
    const style = [names.subfamily, names.typographicSubfamily, names.fullName].join(' ').toLowerCase();
    if (!style.includes(weight.toLowerCase())) {
      throw new Error(
        `${path.basename(file)} does not look like Roboto ${weight} ` +
        `(family="${names.family}", style="${names.subfamily}")`,
      );
    }
  }
  return names;
}

/* ------------------------------------------------------------------ */
/*  Fonts                                                              */
/* ------------------------------------------------------------------ */

async function ensureFont(weight) {
  ensureDir(FONT_DIR);
  const file = path.join(FONT_DIR, `Roboto-${weight}.ttf`);

  if (nonEmptyFile(file, 50_000)) {
    const buf = fs.readFileSync(file);
    try {
      assertRoboto(file, buf, weight);
      step('fonts', `cached ${path.relative(ROOT, file)} (${fmtBytes(buf.length)})`);
      return file;
    } catch (err) {
      step('fonts', `cached ${path.basename(file)} is unusable (${err.message}), re-downloading`);
    }
  }

  const url = `${FONT_URL_BASE}/Roboto-${weight}.ttf`;
  step('fonts', `downloading ${url}`);
  const buf = await download(url, `Roboto ${weight}`);
  const names = assertRoboto(file, buf, weight);
  fs.writeFileSync(file, buf);
  step('fonts', `saved ${path.relative(ROOT, file)} (${fmtBytes(buf.length)}, family "${names.family}")`);
  return file;
}

/* --- lv_font_conv output post-processing -------------------------- */

const LV_CACHE_PREFIX = 'lv_font_conv';

/** Strip the per-run banner comment and the `lvgl.h` include block. */
function stripFontPreamble(text) {
  let out = text;
  out = out.replace(/^\/\*[\s\S]*?\*\/[ \t]*\r?\n/, '');
  out = out.replace(/#ifdef\s+LV_LVGL_H_INCLUDE_SIMPLE[\s\S]*?#endif[ \t]*\r?\n/, '');
  out = out.replace(/^#include\s+.*\r?\n/m, '');
  return out.replace(/^\s+/, '');
}

/**
 * lv_font_conv emits file-scope statics with generic names (`glyph_bitmap`,
 * `cmaps`, `font_dsc`, ...) because it assumes one font per translation unit.
 * We concatenate several fonts into one .c file, so prefix every declaration
 * and every right-hand-side reference with the font symbol.
 *
 * Struct member designators (`.glyph_bitmap = ...`) are left untouched: they
 * only appear on the left of `=`, never immediately after `=` or `&`.
 */
const STATIC_SYMBOLS = [
  'glyph_bitmap', 'glyph_dsc', 'cmaps', 'cache', 'font_dsc',
  'kern_pair_glyph_ids', 'kern_pair_values', 'kern_pairs',
  'kern_classes', 'kern_left_class_mapping', 'kern_right_class_mapping',
  'kern_left_values', 'kern_right_values',
];
/** Shared alternation, including the numbered `unicode_list_<n>` cmaps tables. */
const SYMBOL_ALTERNATION = [...STATIC_SYMBOLS, 'unicode_list_\\d+'].join('|');
const STATIC_SYMBOL_RE = new RegExp(`\\b(${SYMBOL_ALTERNATION})\\b`, 'g');
const RHS_SYMBOL_RE = new RegExp(`([=&][ \\t]*)(${SYMBOL_ALTERNATION})\\b`, 'g');

function renameFontSymbols(text, symbol) {
  const lines = text.split('\n').map((line) =>
    /^\s*static\b/.test(line) ? line.replace(STATIC_SYMBOL_RE, (m) => `${symbol}_${m}`) : line,
  );
  const joined = lines.join('\n');
  // `[=&]` + optional spaces + symbol, e.g. `.cmaps = cmaps,` / `.kern_dsc = &kern_pairs,`.
  return joined.replace(RHS_SYMBOL_RE, (_m, lead, name) => `${lead}${symbol}_${name}`);
}

function runFontConv(spec, ttfPath, tmpDir) {
  const outFile = path.join(tmpDir, `${spec.symbol}.c`);
  const cli = path.join(ROOT, 'node_modules', 'lv_font_conv', 'lv_font_conv.js');
  const args = [
    '--size', String(spec.size),
    '--bpp', '4',
    '--format', 'lvgl',
    '--no-compress',
    '--lv-include', 'lvgl.h',
    '--lv-font-name', spec.symbol,
    '--font', ttfPath,
    '-r', GLYPH_RANGES,
    '-o', outFile,
  ];

  const res = spawnSync(process.execPath, [cli, ...args], {
    cwd: ROOT,
    encoding: 'utf8',
    env: { ...process.env, NODE_OPTIONS: '' },
  });
  if (res.status !== 0) {
    throw new Error(
      `lv_font_conv failed for ${spec.symbol} (exit ${res.status}):\n${res.stderr || res.stdout}`,
    );
  }
  if (!nonEmptyFile(outFile, 100)) throw new Error(`lv_font_conv produced no output for ${spec.symbol}`);

  const raw = fs.readFileSync(outFile, 'utf8');
  return {
    text: renameFontSymbols(stripFontPreamble(raw), spec.symbol),
    rawBytes: raw.length,
    bytes: raw.length,
  };
}

async function buildFonts() {
  ensureDir(OUT_DIR);

  const ttfs = new Map();
  for (const weight of ['Regular', 'Medium', 'Bold']) {
    ttfs.set(weight, await ensureFont(weight));
  }

  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), `${LV_CACHE_PREFIX}_`));
  const parts = [];
  let rawTotal = 0;

  try {
    for (const spec of FONT_SPECS) {
      const ttf = ttfs.get(spec.weight);
      const { text, rawBytes } = runFontConv(spec, ttf, tmpDir);
      rawTotal += rawBytes;
      step('fonts', `built ${spec.symbol} (Roboto ${spec.weight} ${spec.size} px, 4 bpp) - ${fmtBytes(rawBytes)}`);

      const relTtf = path.relative(ROOT, ttf).split(path.sep).join('/');
      parts.push({
        spec,
        relTtf,
        text: text.trimEnd(),
      });
    }
  } finally {
    fs.rmSync(tmpDir, { recursive: true, force: true });
  }

  /* ---- fonts.c ---- */
  const lines = [];
  lines.push('/**');
  lines.push(' * @file fonts.c');
  lines.push(' *');
  lines.push(' * AUTO-GENERATED by scripts/gen_assets.mjs - do not edit by hand.');
  lines.push(' *');
  lines.push(' * Source font: Roboto (https://fonts.google.com/specimen/Roboto),');
  lines.push(' * Copyright 2011 Google Inc., licensed under the Apache License 2.0.');
  lines.push(' * Rasterised with lv_font_conv: --bpp 4 --format lvgl --no-compress.');
  lines.push(` * Glyph ranges: ${GLYPH_RANGES}`);
  lines.push(' */');
  lines.push('');
  lines.push('#include "fonts.h"');
  lines.push('');

  for (const part of parts) {
    lines.push('/*===========================================================================');
    lines.push(` * ${part.spec.symbol} - Roboto ${part.spec.weight} ${part.spec.size} px, 4 bpp`);
    lines.push(` * source: ${part.relTtf}`);
    lines.push(' *=========================================================================*/');
    lines.push('');
    lines.push(part.text);
    lines.push('');
  }
  lines.push('');

  const fontsC = lines.join('\n');

  /* ---- fonts.h ---- */
  const h = [];
  h.push('/**');
  h.push(' * @file fonts.h');
  h.push(' *');
  h.push(' * AUTO-GENERATED by scripts/gen_assets.mjs - do not edit by hand.');
  h.push(' * Roboto 4 bpp LVGL 9 fonts (Apache-2.0).');
  h.push(' */');
  h.push('');
  h.push('#ifndef MAIN_ASSETS_FONTS_H');
  h.push('#define MAIN_ASSETS_FONTS_H');
  h.push('');
  h.push('#include "lvgl.h"');
  h.push('');
  h.push('#ifdef __cplusplus');
  h.push('extern "C" {');
  h.push('#endif');
  h.push('');
  h.push(`/* ${FONT_SPECS.length} fonts: Roboto Regular/Medium/Bold, 4 bpp, glyphs ${GLYPH_RANGES} */`);
  const width = Math.max(...FONT_SPECS.map((f) => f.symbol.length)) + 1;
  for (const f of FONT_SPECS) {
    const decl = `extern const lv_font_t ${f.symbol};`;
    h.push(`${decl.padEnd(decl.length + (width - f.symbol.length))}  /* Roboto ${f.weight} ${f.size} px */`);
  }
  h.push('');
  h.push('#ifdef __cplusplus');
  h.push('}');
  h.push('#endif');
  h.push('');
  h.push('#endif /* MAIN_ASSETS_FONTS_H */');
  h.push('');

  const fontsH = h.join('\n');

  const changedC = writeIfChanged(FONTS_C, fontsC);
  const changedH = writeIfChanged(FONTS_H, fontsH);
  step('fonts', `main/assets/fonts.c ${changedC ? 'written' : 'unchanged'} (${fmtBytes(fontsC.length)})`);
  step('fonts', `main/assets/fonts.h ${changedH ? 'written' : 'unchanged'} (${fmtBytes(fontsH.length)})`);

  return { fontsC, fontsH, rawTotal, count: FONT_SPECS.length };
}

/* ------------------------------------------------------------------ */
/*  Icons                                                              */
/* ------------------------------------------------------------------ */

/** Force `stroke-width` on the root <svg> element, leaving child paths alone.
 *
 *  The cached SVG on disk stays byte-identical to upstream (it is gitignored
 *  and re-fetched as needed); the patch is applied in memory right before
 *  rasterising, so the icons keep their hairline-safe weight without touching
 *  the source files.
 */
function forceStrokeWidth(svg, width) {
  return svg.replace(/<svg\b[^>]*>/i, (tag) => {
    if (/stroke-width\s*=\s*"/i.test(tag)) {
      return tag.replace(/stroke-width\s*=\s*"[^"]*"/i, `stroke-width="${width}"`);
    }
    return tag.replace(/<svg\b/i, `<svg stroke-width="${width}"`);
  });
}

/** Lucide embeds its canonical name in the root class list, e.g. `lucide-sun`. */
function svgIconName(svg) {
  const m = /class="[^"]*\blucide-([a-z0-9][a-z0-9-]*)/i.exec(svg);
  return m ? m[1] : null;
}

async function ensureIcon(name) {
  const file = path.join(ICON_DIR, `${name}.svg`);
  if (nonEmptyFile(file, 50)) {
    const svg = fs.readFileSync(file, 'utf8');
    if (svg.includes('<svg')) return { name, svg, file, slug: name, fromCache: true };
    step('icons', `cached ${name}.svg is corrupt, re-downloading`);
  }

  const slugs = ICON_ALIASES[name] ?? [name];
  let lastErr;
  for (const slug of slugs) {
    const url = `https://unpkg.com/lucide-static@latest/icons/${slug}.svg`;
    try {
      const buf = await download(url, `lucide ${slug}`);
      const svg = buf.toString('utf8');
      if (!svg.includes('<svg')) throw new Error(`response for ${slug} is not an SVG`);
      fs.writeFileSync(file, buf);
      if (slug !== name) step('icons', `${name}: upstream renamed, used "${slug}"`);
      return { name, svg, file, slug, fromCache: false };
    } catch (err) {
      lastErr = err;
    }
  }
  throw new Error(`could not fetch Lucide icon "${name}": ${lastErr?.message}`);
}

/** Rasterise one patched SVG and return the alpha coverage bitmap. */
function rasterizeAlpha(svg, px) {
  const resvg = new Resvg(svg, {
    fitTo: { mode: 'width', value: px },
    background: 'rgba(0,0,0,0)',
    font: { loadSystemFonts: false, defaultFontFamily: 'sans-serif' },
  });
  const img = resvg.render();
  const w = img.width;
  const h = img.height;
  const rgba = img.pixels; // RGBA, length = w * h * 4
  if (rgba.length !== w * h * 4) {
    throw new Error(`unexpected RGBA buffer size ${rgba.length} for ${w}x${h}`);
  }
  const alpha = new Uint8Array(w * h);
  for (let i = 0, p = 0; i < alpha.length; i++, p += 4) alpha[i] = rgba[p + 3];
  return { w, h, alpha };
}

/**
 * Pack 8-bit alpha pixels: one byte per pixel, row major, stride = w.
 * This is LVGL's LV_COLOR_FORMAT_A8, the alpha-only format the software
 * renderer actually blits (A1/A2/A4 are not handled by lv_draw_sw_blend).
 */
function packA8(w, h, alpha) {
  const stride = w; // one byte per pixel
  const out = Buffer.alloc(stride * h);
  out.set(alpha); // resvg gives us 8-bit coverage already
  return { data: out, stride };
}

function hexArray(body, indent = '    ') {
  const lines = [];
  for (let i = 0; i < body.length; i += 16) {
    const chunk = body.subarray(i, Math.min(i + 16, body.length));
    lines.push(indent + [...chunk].map((b) => `0x${b.toString(16).padStart(2, '0')}`).join(', ') + ',');
  }
  return lines.join('\n');
}

const iconSymbol = (name, size) => `icon_${name.replace(/-/g, '_')}_${size}`;

async function buildIcons() {
  ensureDir(ICON_DIR);
  ensureDir(OUT_DIR);

  const built = [];
  for (const name of ICONS) {
    const { svg, slug } = await ensureIcon(name);
    // Canonical Lucide slug: a cached file keeps its requested name on disk, so
    // recover the upstream name from the SVG itself for logging/attribution.
    const upstream = svgIconName(svg) ?? slug;
    const patched = forceStrokeWidth(svg, ICON_STROKE_WIDTH);
    if (!new RegExp(`stroke-width="${String(ICON_STROKE_WIDTH).replace('.', '\\.')}"`).test(patched)) {
      throw new Error(`failed to force stroke-width on ${name}`);
    }

    const variants = [];
    for (const size of ICON_SIZES) {
      const { w, h, alpha } = rasterizeAlpha(patched, size);
      if (w !== size || h !== size) {
        step('icons', `warning: ${name} rendered ${w}x${h} for target ${size}px, using actual size`);
      }
      const { data, stride } = packA8(w, h, alpha);
      variants.push({ size, w, h, stride, data, symbol: iconSymbol(name, size) });
    }

    built.push({ name, upstream, variants });
    step(
      'icons',
      `${name}${upstream !== name ? ` (upstream ${upstream})` : ''} -> ` +
      variants.map((v) => `${v.w}x${v.h} ${v.stride * v.h} B`).join(', '),
    );
  }

  /* ---- icons.c ---- */
  const lines = [];
  lines.push('/**');
  lines.push(' * @file icons.c');
  lines.push(' *');
  lines.push(' * AUTO-GENERATED by scripts/gen_assets.mjs - do not edit by hand.');
  lines.push(' *');
  lines.push(` * Icons: Lucide (https://lucide.dev), ISC License, v1.72.x static SVGs.`);
  lines.push(` * Rasterised with @resvg/resvg-js at ${ICON_SIZES.join('/')} px, stroke-width ${ICON_STROKE_WIDTH},`);
  lines.push(' * alpha channel stored as LVGL 9 LV_COLOR_FORMAT_A8 (8 bpp, one byte per');
  lines.push(' * pixel, no colour, stride = w).');
  lines.push(' */');
  lines.push('');
  lines.push('#include "icons.h"');
  lines.push('');
  lines.push('#include <stddef.h>');
  lines.push('#include <string.h>');
  lines.push('');
  lines.push('/*===========================================================================');
  lines.push(' * Pixel data (LV_COLOR_FORMAT_A8)');
  lines.push(' *=========================================================================*/');
  lines.push('');

  for (const icon of built) {
    const upstream = icon.upstream === icon.name ? '' : `, upstream ${icon.upstream}.svg`;
    for (const v of icon.variants) {
      lines.push(`/* ${icon.name}${upstream} - ${v.w}x${v.h}, stride ${v.stride} B, ${v.stride * v.h} B */`);
      lines.push(`static const LV_ATTRIBUTE_MEM_ALIGN uint8_t ${v.symbol}_map[] = {`);
      lines.push(hexArray(v.data));
      lines.push('};');
      lines.push('');
    }
  }

  lines.push('/*===========================================================================');
  lines.push(' * Image descriptors');
  lines.push(' *=========================================================================*/');
  lines.push('');

  for (const icon of built) {
    for (const v of icon.variants) {
      lines.push(`const lv_image_dsc_t ${v.symbol} = {`);
      lines.push('    .header = {');
      lines.push('        .magic = LV_IMAGE_HEADER_MAGIC,');
      lines.push('        .cf = LV_COLOR_FORMAT_A8,');
      lines.push('        .flags = 0,');
      lines.push(`        .w = ${v.w},`);
      lines.push(`        .h = ${v.h},`);
      lines.push(`        .stride = ${v.stride},`);
      lines.push('    },');
      lines.push(`    .data_size = sizeof(${v.symbol}_map),`);
      lines.push(`    .data = ${v.symbol}_map,`);
      lines.push('};');
      lines.push('');
    }
  }



  lines.push('/*===========================================================================');
  lines.push(' * Name lookup');
  lines.push(' *=========================================================================*/');
  lines.push('');
  lines.push('typedef struct {');
  lines.push('    const char * name;');
  lines.push('    const lv_image_dsc_t * dsc_24;');
  lines.push('    const lv_image_dsc_t * dsc_48;');
  lines.push('} icon_entry_t;');
  lines.push('');
  lines.push('static const icon_entry_t icon_table[] = {');
  const nameWidth = Math.max(...built.map((i) => i.name.length)) + 2;
  for (const icon of built) {
    const n = `"${icon.name}",`.padEnd(nameWidth + 2);
    lines.push(`    { ${n} &${iconSymbol(icon.name, 24)}, &${iconSymbol(icon.name, 48)} },`);
  }
  lines.push('};');
  lines.push('');
  lines.push('const lv_image_dsc_t * icon_get(const char * name, int size)');
  lines.push('{');
  lines.push('    if (name == NULL) return NULL;');
  lines.push('');
  lines.push('    for (size_t i = 0; i < sizeof(icon_table) / sizeof(icon_table[0]); i++) {');
  lines.push('        if (strcmp(icon_table[i].name, name) == 0) {');
  lines.push(`            return (size >= ${ICON_LARGE_THRESHOLD}) ? icon_table[i].dsc_48 : icon_table[i].dsc_24;`);
  lines.push('        }');
  lines.push('    }');
  lines.push('    return NULL;');
  lines.push('}');
  lines.push('');

  const iconsC = lines.join('\n');

  /* ---- icons.h ---- */
  const h = [];
  h.push('/**');
  h.push(' * @file icons.h');
  h.push(' *');
  h.push(' * AUTO-GENERATED by scripts/gen_assets.mjs - do not edit by hand.');
  h.push(` * Lucide icons (ISC) as ${ICON_SIZES.join('/')} px LVGL 9 A8 alpha images.`);
  h.push(' */');
  h.push('');
  h.push('#ifndef MAIN_ASSETS_ICONS_H');
  h.push('#define MAIN_ASSETS_ICONS_H');
  h.push('');
  h.push('#include "lvgl.h"');
  h.push('');
  h.push('#ifdef __cplusplus');
  h.push('extern "C" {');
  h.push('#endif');
  h.push('');
  h.push(`/* ${built.length} icons x ${ICON_SIZES.length} sizes = ${built.length * ICON_SIZES.length} descriptors */`);
  for (const icon of built) {
    for (const size of ICON_SIZES) {
      h.push(`extern const lv_image_dsc_t ${iconSymbol(icon.name, size)};`);
    }
  }
  h.push('');
  h.push('/**');
  h.push(' * Look up an icon by its Lucide name (e.g. "cloud-rain").');
  h.push(' *');
  h.push(` * @param name  icon name, dashes included`);
  h.push(` * @param size  requested pixel size; >= ${ICON_LARGE_THRESHOLD} returns the ${ICON_SIZES[1]} px variant,`);
  h.push(` *              anything smaller returns the ${ICON_SIZES[0]} px variant`);
  h.push(' * @return      the image descriptor, or NULL when the name is unknown');
  h.push(' */');
  h.push('const lv_image_dsc_t * icon_get(const char * name, int size);');
  h.push('');
  h.push('#ifdef __cplusplus');
  h.push('}');
  h.push('#endif');
  h.push('');
  h.push('#endif /* MAIN_ASSETS_ICONS_H */');
  h.push('');

  const iconsH = h.join('\n');

  const changedC = writeIfChanged(ICONS_C, iconsC);
  const changedH = writeIfChanged(ICONS_H, iconsH);
  step('icons', `main/assets/icons.c ${changedC ? 'written' : 'unchanged'} (${fmtBytes(iconsC.length)})`);
  step('icons', `main/assets/icons.h ${changedH ? 'written' : 'unchanged'} (${fmtBytes(iconsH.length)})`);

  const payload = built.reduce(
    (n, i) => n + i.variants.reduce((m, v) => m + v.data.length, 0),
    0,
  );

  return { iconsC, iconsH, count: built.length, payload, built };
}

/* ------------------------------------------------------------------ */
/*  Validation                                                         */
/* ------------------------------------------------------------------ */

function countMatches(text, re) {
  return (text.match(re) ?? []).length;
}

function validate(fonts, icons) {
  const problems = [];

  /* ---- fonts.c ---- */
  const fontsC = fonts.fontsC;
  const fontsH = fonts.fontsH;

  const fontDefs = [...fontsC.matchAll(/^const lv_font_t (\w+) = \{/gm)].map((m) => m[1]);
  if (fontDefs.length !== FONT_SPECS.length) {
    problems.push(`fonts.c defines ${fontDefs.length} lv_font_t, expected ${FONT_SPECS.length}`);
  }
  for (const spec of FONT_SPECS) {
    if (!fontDefs.includes(spec.symbol)) problems.push(`fonts.c missing definition of ${spec.symbol}`);
    if (!new RegExp(`extern const lv_font_t ${spec.symbol}\\b`).test(fontsH)) {
      problems.push(`fonts.h missing declaration of ${spec.symbol}`);
    }
  }
  if (countMatches(fontsC, /\.bpp = 4,/g) !== FONT_SPECS.length) {
    problems.push('fonts.c: not every font is 4 bpp');
  }
  if (/\.bitmap_format = 1/.test(fontsC)) {
    problems.push('fonts.c: compressed bitmap format found despite --no-compress');
  }
  // Every bitmap array must be declared `static LV_ATTRIBUTE_LARGE_CONST const uint8_t`.
  const bitmapDecls = countMatches(fontsC, /^static LV_ATTRIBUTE_LARGE_CONST const uint8_t (\w+)\[\] = \{$/gm);
  if (bitmapDecls !== FONT_SPECS.length) {
    problems.push(`fonts.c: found ${bitmapDecls} glyph bitmap arrays, expected ${FONT_SPECS.length}`);
  }
  // Every bitmap array must be reachable from a font descriptor.
  const bitmapNames = [...fontsC.matchAll(/^static LV_ATTRIBUTE_LARGE_CONST const uint8_t (\w+)\[\]/gm)].map((m) => m[1]);
  for (const name of bitmapNames) {
    if (!fontsC.includes(`.glyph_bitmap = ${name},`)) {
      problems.push(`fonts.c: bitmap array ${name} is not referenced by a font descriptor`);
    }
  }

  /* ---- icons.c ---- */
  const iconsC = icons.iconsC;
  const iconsH = icons.iconsH;

  const dscNames = [...iconsC.matchAll(/^const lv_image_dsc_t (\w+) = \{/gm)].map((m) => m[1]);
  const expected = icons.count * ICON_SIZES.length;
  if (dscNames.length !== expected) {
    problems.push(`icons.c defines ${dscNames.length} descriptors, expected ${expected}`);
  }
  if (new Set(dscNames).size !== dscNames.length) problems.push('icons.c has duplicate descriptor names');

  const staticArrays = new Set(
    [...iconsC.matchAll(/^static const LV_ATTRIBUTE_MEM_ALIGN uint8_t (\w+)\[\] = \{$/gm)].map((m) => m[1]),
  );

  // Inspect each descriptor block (`const lv_image_dsc_t <sym> = { ... };`).
  const blocks = [...iconsC.matchAll(/^const lv_image_dsc_t (\w+) = \{\n([\s\S]*?)^\};\n/gm)];
  if (blocks.length !== dscNames.length) {
    problems.push(`icons.c has ${blocks.length} parseable descriptor bodies, expected ${dscNames.length}`);
  }
  for (const [, sym, block] of blocks) {
    const magic = /\.magic = LV_IMAGE_HEADER_MAGIC,/.test(block);
    const cf = /\.cf = LV_COLOR_FORMAT_A8,/.test(block);
    const dataRef = /\.data = (\w+),/.exec(block);
    const sizeRef = /\.data_size = sizeof\((\w+)\),/.exec(block);
    const w = Number(/\.w = (\d+),/.exec(block)?.[1] ?? -1);
    const h = Number(/\.h = (\d+),/.exec(block)?.[1] ?? -1);
    const stride = Number(/\.stride = (\d+),/.exec(block)?.[1] ?? -1);

    if (!magic) problems.push(`${sym}: missing LV_IMAGE_HEADER_MAGIC`);
    if (!cf) problems.push(`${sym}: missing LV_COLOR_FORMAT_A8`);
    if (!dataRef) problems.push(`${sym}: no .data pointer`);
    if (!sizeRef) problems.push(`${sym}: no .data_size`);
    if (w <= 0 || h <= 0 || stride <= 0) problems.push(`${sym}: bad w/h/stride`);
    if (stride !== w) problems.push(`${sym}: stride ${stride} != width ${w} (A8 is 1 byte/px)`);
    if (dataRef && !staticArrays.has(dataRef[1])) problems.push(`${sym}: .data points at undefined array ${dataRef[1]}`);
    if (dataRef && sizeRef && dataRef[1] !== sizeRef[1]) {
      problems.push(`${sym}: .data_size sizeof(${sizeRef[1]}) does not match .data ${dataRef[1]}`);
    }

    // Byte count of the referenced array must equal stride * h (= w * h for A8).
    if (dataRef && staticArrays.has(dataRef[1])) {
      const arr = new RegExp(`^static const LV_ATTRIBUTE_MEM_ALIGN uint8_t ${dataRef[1]}\\[\\] = \\{\\n([\\s\\S]*?)\\n\\};`, 'm')
        .exec(iconsC);
      const bytes = arr ? (arr[1].match(/0x[0-9a-f]{2}/g) ?? []).length : -1;
      if (bytes !== stride * h) problems.push(`${sym}: ${dataRef[1]} has ${bytes} bytes, expected ${stride * h}`);
      if (bytes !== w * h) problems.push(`${sym}: ${dataRef[1]} has ${bytes} bytes, expected w*h = ${w * h}`);
    }
  }

  // Header/implementation agreement.
  for (const sym of dscNames) {
    if (!new RegExp(`extern const lv_image_dsc_t ${sym};`).test(iconsH)) {
      problems.push(`icons.h missing extern for ${sym}`);
    }
  }
  if (countMatches(iconsH, /^extern const lv_image_dsc_t /gm) !== dscNames.length) {
    problems.push('icons.h exports a different number of descriptors than icons.c defines');
  }
  if (!/const lv_image_dsc_t \* icon_get\(const char \* name, int size\);/.test(iconsH)) {
    problems.push('icons.h missing icon_get() prototype');
  }

  // Alpha sanity: every icon must have visible pixels and exactly w*h bytes.
  for (const icon of icons.built) {
    for (const v of icon.variants) {
      const any = v.data.some((b) => b !== 0);
      if (!any) problems.push(`${v.symbol}: rendered completely empty`);
      if (v.data.length !== v.stride * v.h) problems.push(`${v.symbol}: packed size mismatch`);
      if (v.data.length !== v.w * v.h) problems.push(`${v.symbol}: expected w*h bytes`);
      const full = v.data.some((b) => b === 255);
      if (!full) problems.push(`${v.symbol}: no fully opaque pixel (alpha scaling looks wrong)`);
    }
  }

  // The lookup table must cover every icon.
  for (const icon of icons.built) {
    if (!iconsC.includes(`{ "${icon.name}",`)) problems.push(`icon_table missing ${icon.name}`);
  }

  // No leftover un-prefixed generic statics (would clash in one TU).
  const genericStatics = countMatches(
    iconsC + fontsC,
    /^static[^(\n]*\b(glyph_bitmap|font_dsc|cmaps|kern_pairs|glyph_dsc|font_cache)\b/mg,
  );
  if (genericStatics > 0) problems.push(`found ${genericStatics} un-prefixed generic static symbol(s)`);

  // Duplicate file-scope symbols across the concatenated fonts. LVGL 8/9
  // fallback branches define the same symbol under `#else`, which is fine.
  const declLines = fontsC.split('\n');
  const decls = [];
  for (let i = 0; i < declLines.length; i++) {
    const m = /^static[^=;\n]*?\b(\w+)(\[\])?\s*(=|;)/.exec(declLines[i]);
    if (!m) continue;
    const prev = declLines.slice(0, i).reverse().find((l) => l.trim() !== '');
    if (prev && prev.trim() === '#else') continue;
    decls.push(m[1]);
  }
  const dupes = decls.filter((d, i) => decls.indexOf(d) !== i);
  if (dupes.length) problems.push(`duplicate file-scope symbols in fonts.c: ${[...new Set(dupes)].join(', ')}`);

  return problems;
}

/* ------------------------------------------------------------------ */
/*  Clean                                                              */
/* ------------------------------------------------------------------ */

function clean() {
  for (const f of [FONTS_C, FONTS_H, ICONS_C, ICONS_H]) {
    if (fs.existsSync(f)) {
      fs.rmSync(f);
      step('clean', `removed ${path.relative(ROOT, f)}`);
    }
  }
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

async function main() {
  const argv = new Set(process.argv.slice(2));
  if (argv.has('--clean')) {
    clean();
    return;
  }

  ensureDir(OUT_DIR);
  log(`gen_assets: project root ${ROOT}`);

  const fonts = await buildFonts();
  const icons = await buildIcons();

  step('check', 'validating generated sources');
  const problems = validate(fonts, icons);
  if (problems.length) {
    console.error('');
    for (const p of problems) console.error(`  FAIL ${p}`);
    throw new Error(`${problems.length} validation problem(s)`);
  }
  step('check', `ok: ${fonts.count} fonts, ${icons.count} icons, ${icons.count * ICON_SIZES.length} image descriptors`);

  const fontDataBytes = fonts.fontsC.length + fonts.fontsH.length;
  const iconDataBytes = icons.iconsC.length + icons.iconsH.length;

  log('');
  log('summary');
  log(`  fonts      : ${fonts.count} (4 bpp) -> main/assets/fonts.c, fonts.h`);
  log(`  icons      : ${icons.count} names x ${ICON_SIZES.length} sizes (A8) -> main/assets/icons.c, icons.h`);
  log(`  icon pixel payload : ${icons.payload} B`);
  log(`  generated C size   : ${fmtBytes(fontDataBytes)} fonts, ${fmtBytes(iconDataBytes)} icons, total ${fmtBytes(fontDataBytes + iconDataBytes)}`);
}

main().catch((err) => {
  console.error(`gen_assets: ${err.message}`);
  process.exitCode = 1;
});

