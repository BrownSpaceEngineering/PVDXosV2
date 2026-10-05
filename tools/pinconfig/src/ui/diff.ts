// Line diff for the Changes view. Trims the common head and tail, then runs an
// LCS on the middle part, which is small for the edits this tool makes.

export interface DiffLine {
    op: ' ' | '-' | '+';
    text: string;
    oldNo?: number;
    newNo?: number;
}

export interface Hunk {
    lines: DiffLine[];
}

const MAX_CELLS = 16_000_000;

export function diffLines(oldText: string, newText: string): DiffLine[] {
    const a = oldText.split('\n');
    const b = newText.split('\n');
    let head = 0;
    while (head < a.length && head < b.length && a[head] === b[head]) head++;
    let tail = 0;
    while (tail < a.length - head && tail < b.length - head && a[a.length - 1 - tail] === b[b.length - 1 - tail]) tail++;
    const am = a.slice(head, a.length - tail);
    const bm = b.slice(head, b.length - tail);

    const out: DiffLine[] = [];
    for (let i = 0; i < head; i++) out.push({ op: ' ', text: a[i], oldNo: i + 1, newNo: i + 1 });

    if (am.length * bm.length > MAX_CELLS) {
        am.forEach((t, i) => out.push({ op: '-', text: t, oldNo: head + i + 1 }));
        bm.forEach((t, i) => out.push({ op: '+', text: t, newNo: head + i + 1 }));
    } else {
        const n = am.length;
        const m = bm.length;
        const lcs: Uint32Array[] = Array.from({ length: n + 1 }, () => new Uint32Array(m + 1));
        for (let i = n - 1; i >= 0; i--) {
            for (let j = m - 1; j >= 0; j--) {
                lcs[i][j] = am[i] === bm[j] ? lcs[i + 1][j + 1] + 1 : Math.max(lcs[i + 1][j], lcs[i][j + 1]);
            }
        }
        let i = 0;
        let j = 0;
        while (i < n || j < m) {
            if (i < n && j < m && am[i] === bm[j]) {
                out.push({ op: ' ', text: am[i], oldNo: head + i + 1, newNo: head + j + 1 });
                i++;
                j++;
            } else if (i < n && (j === m || lcs[i + 1][j] >= lcs[i][j + 1])) {
                out.push({ op: '-', text: am[i], oldNo: head + i + 1 });
                i++;
            } else {
                out.push({ op: '+', text: bm[j], newNo: head + j + 1 });
                j++;
            }
        }
    }
    const shift = b.length - a.length;
    for (let k = a.length - tail; k < a.length; k++) out.push({ op: ' ', text: a[k], oldNo: k + 1, newNo: k + 1 + shift });
    return out;
}

// Groups changed lines with `context` unchanged lines around them.
export function hunks(lines: DiffLine[], context = 3): Hunk[] {
    const keep = new Array<boolean>(lines.length).fill(false);
    lines.forEach((l, i) => {
        if (l.op === ' ') return;
        for (let k = Math.max(0, i - context); k <= Math.min(lines.length - 1, i + context); k++) keep[k] = true;
    });
    const out: Hunk[] = [];
    let current: Hunk | null = null;
    lines.forEach((l, i) => {
        if (!keep[i]) {
            current = null;
            return;
        }
        if (!current) {
            current = { lines: [] };
            out.push(current);
        }
        current.lines.push(l);
    });
    return out;
}
