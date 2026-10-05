import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { readAtzip, readText, type Atzip } from '../src/core/atzip.ts';

const repo = fileURLToPath(new URL('../../../', import.meta.url));

export const REFERENCES = {
    final: `${repo}PVDX-SAMD-PinConfig/FinalCompute.atzip`,
    old: `${repo}ASF.atzip`,
};

export async function load(path: string): Promise<Atzip> {
    return readAtzip(readFileSync(path));
}

// Short line diff for readable test failures.
export function firstDifference(expected: string, actual: string): string {
    const a = expected.split('\n');
    const b = actual.split('\n');
    for (let i = 0; i < Math.max(a.length, b.length); i++) {
        if (a[i] !== b[i]) {
            const ctx = (l: string[]) => l.slice(Math.max(0, i - 2), i + 3).map((x) => JSON.stringify(x)).join('\n');
            return `line ${i + 1}\n--- expected\n${ctx(a)}\n--- actual\n${ctx(b)}`;
        }
    }
    return 'identical';
}

export function diffLines(zip: Atzip, path: string, text: string): string[] {
    const a = readText(zip, path).split('\n');
    const b = text.split('\n');
    const out: string[] = [];
    // Files keep their line count in the edit tests, so a positional diff is enough there.
    for (let i = 0; i < Math.max(a.length, b.length); i++) if (a[i] !== b[i]) out.push(`${a[i] ?? ''} => ${b[i] ?? ''}`);
    return out;
}
