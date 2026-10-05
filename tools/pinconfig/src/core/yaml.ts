// Reads and writes atmel_start_config.atstart.
//
// Atmel START wrote this file with PyYAML (block style, 80 column folding, YAML 1.1
// scalars). The emitter below reproduces PyYAML's output for the subset of YAML that
// the file uses, so that loading and saving an unchanged project is byte-identical.
import { parse } from 'yaml';

export type YamlValue = null | boolean | number | string | YamlValue[] | YamlMap;
export interface YamlMap {
    [key: string]: YamlValue;
}

export function parseAtstart(text: string): YamlMap {
    return parse(text, { version: '1.1', intAsBigInt: false }) as YamlMap;
}

const BEST_WIDTH = 80;
const INDENT = 2;

// YAML 1.1 implicit resolvers used by PyYAML. A string that matches one of these must be quoted.
const IMPLICIT = [
    /^(?:yes|Yes|YES|no|No|NO|true|True|TRUE|false|False|FALSE|on|On|ON|off|Off|OFF)$/,
    /^(?:[-+]?0b[0-1_]+|[-+]?0[0-7_]+|[-+]?(?:0|[1-9][0-9_]*)|[-+]?0x[0-9a-fA-F_]+|[-+]?[1-9][0-9_]*(?::[0-5]?[0-9])+)$/,
    /^(?:[-+]?(?:[0-9][0-9_]*)\.[0-9_]*(?:[eE][-+][0-9]+)?|\.[0-9_]+(?:[eE][-+][0-9]+)?|[-+]?[0-9][0-9_]*(?::[0-5]?[0-9])+\.[0-9_]*|[-+]?\.(?:inf|Inf|INF)|\.(?:nan|NaN|NAN))$/,
    /^(?:~|null|Null|NULL|)$/,
    /^(?:[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]|[0-9][0-9][0-9][0-9]-[0-9][0-9]?-[0-9][0-9]?(?:[Tt]|[ \t]+)[0-9][0-9]?:[0-9][0-9]:[0-9][0-9](?:\.[0-9]*)?(?:[ \t]*(?:Z|[-+][0-9][0-9]?(?::[0-9][0-9])?))?)$/,
    /^<<$/,
    /^=$/,
];

function plainAllowed(s: string): boolean {
    if (s === '' || IMPLICIT.some((re) => re.test(s))) return false;
    if (s.startsWith(' ') || s.endsWith(' ')) return false;
    if (s.startsWith('---') || s.startsWith('...')) return false;
    for (let i = 0; i < s.length; i++) {
        const ch = s[i];
        const next = s[i + 1];
        const followedBySpace = next === undefined || next === ' ' || next === '\n';
        const precededBySpace = i === 0 || s[i - 1] === ' ';
        if (ch === '\n' || ch === '\t' || ch < ' ' || ch > '~') return false;
        if (i === 0) {
            if ('#,[]{}&*!|>\'"%@`'.includes(ch)) return false;
            if ((ch === '?' || ch === ':' || ch === '-') && followedBySpace) return false;
        } else {
            if (ch === ':' && followedBySpace) return false;
            if (ch === '#' && precededBySpace) return false;
        }
    }
    return true;
}

class Emitter {
    out = '';
    column = 0;

    write(text: string): void {
        this.out += text;
        const nl = text.lastIndexOf('\n');
        this.column = nl < 0 ? this.column + text.length : text.length - nl - 1;
    }

    newline(indent: number): void {
        this.write('\n' + ' '.repeat(indent));
    }

    // Folds a plain or single-quoted scalar the way PyYAML does: at a single space,
    // break the line when the column is already past the best width.
    folded(text: string, indent: number): void {
        const words = text.split(' ');
        this.write(words[0]);
        for (let i = 1; i < words.length; i++) {
            if (this.column > BEST_WIDTH && words[i - 1] !== '' && words[i] !== '') {
                this.newline(indent);
            } else {
                this.write(' ');
            }
            this.write(words[i]);
        }
    }

    scalar(value: YamlValue, indent: number): void {
        if (value === null) return this.write('null');
        if (typeof value === 'boolean') return this.write(value ? 'true' : 'false');
        if (typeof value === 'number') return this.write(Number.isInteger(value) ? String(value) : pyFloat(value));
        const s = value as string;
        if (plainAllowed(s)) return this.folded(s, indent);
        this.write("'");
        this.folded(s.replace(/'/g, "''"), indent);
        this.write("'");
    }

    // Writes the value that follows "key:" or "- ". `indent` is the indentation of the key.
    value(value: YamlValue, indent: number, inSequence: boolean): void {
        if (Array.isArray(value)) {
            if (value.length === 0) return this.write(' []');
            // PyYAML writes sequences inside mappings without extra indentation.
            const seqIndent = inSequence ? indent + INDENT : indent;
            for (const item of value) {
                this.newline(seqIndent);
                this.write('-');
                this.sequenceItem(item, seqIndent);
            }
            return;
        }
        if (value !== null && typeof value === 'object') {
            const keys = Object.keys(value);
            if (keys.length === 0) return this.write(' {}');
            for (const key of keys) {
                this.newline(indent + INDENT);
                this.mappingEntry(key, value[key], indent + INDENT);
            }
            return;
        }
        this.write(' ');
        this.scalar(value, indent + INDENT);
    }

    sequenceItem(item: YamlValue, seqIndent: number): void {
        if (item !== null && typeof item === 'object' && !Array.isArray(item) && Object.keys(item).length > 0) {
            const keys = Object.keys(item);
            this.write(' ');
            keys.forEach((key, i) => {
                if (i > 0) this.newline(seqIndent + INDENT);
                this.mappingEntry(key, item[key], seqIndent + INDENT);
            });
            return;
        }
        this.value(item, seqIndent, true);
    }

    mappingEntry(key: string, value: YamlValue, indent: number): void {
        this.scalar(key, indent);
        this.write(':');
        this.value(value, indent, false);
    }
}

function pyFloat(n: number): string {
    const s = String(n);
    return s.includes('.') || s.includes('e') ? s : s + '.0';
}

export function emitAtstart(doc: YamlMap): string {
    const e = new Emitter();
    Object.keys(doc).forEach((key, i) => {
        if (i > 0) e.newline(0);
        e.mappingEntry(key, doc[key], 0);
    });
    return e.out + '\n';
}
