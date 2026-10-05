// Builds the pin database from the device header that ships in every SAMD51 .atzip.
// Usage: node scripts/gen-pinmux.ts <samd51p20a.h> <out.json>
import { readFileSync, writeFileSync } from 'node:fs';

const [, , headerPath, outPath] = process.argv;
if (!headerPath || !outPath) {
    console.error('usage: gen-pinmux.ts <samd51p20a.h> <out.json>');
    process.exit(1);
}

const text = readFileSync(headerPath, 'utf8');

const pins: string[] = [];
for (const m of text.matchAll(/^#define PIN_(P[A-D]\d\d)\s+\d+\s/gm)) {
    pins.push(m[1]);
}

interface MuxEntry {
    func: string;
    peripheral: string;
    signal: string;
    macro: string;
}

const functions: Record<string, MuxEntry[]> = {};
for (const pin of pins) functions[pin] = [];
for (const m of text.matchAll(/^#define (PINMUX_(P[A-D]\d\d)([A-N])_([A-Z0-9]+)_(\w+))\s/gm)) {
    const [, macro, pin, func, peripheral, signal] = m;
    functions[pin]?.push({ func, peripheral, signal, macro });
}
for (const pin of pins) functions[pin].sort((a, b) => a.func.localeCompare(b.func) || a.macro.localeCompare(b.macro));

const device = /samd51(\w+)\.h$/.exec(headerPath)?.[1] ?? 'p20a';
writeFileSync(outPath, JSON.stringify({ device: `SAMD51${device.toUpperCase()}`, pins, functions }, null, 1) + '\n');
console.log(`${pins.length} pins, ${Object.values(functions).flat().length} mux entries -> ${outPath}`);
