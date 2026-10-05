import { expect, it } from 'vitest';
import { diffLines, hunks } from '../src/ui/diff.ts';

it('shows a replaced line as a removal followed by an addition', () => {
    const lines = diffLines('a\nb\nc\nd', 'a\nB\nc\nd\ne');
    expect(lines.map((l) => l.op + l.text)).toEqual([' a', '-b', '+B', ' c', ' d', '+e']);
    expect(lines.find((l) => l.text === 'd')).toMatchObject({ oldNo: 4, newNo: 4 });
});

it('groups changes with context', () => {
    const a = Array.from({ length: 30 }, (_, i) => `line ${i}`);
    const b = [...a];
    b[2] = 'changed';
    b[25] = 'changed too';
    expect(hunks(diffLines(a.join('\n'), b.join('\n')), 2)).toHaveLength(2);
});
