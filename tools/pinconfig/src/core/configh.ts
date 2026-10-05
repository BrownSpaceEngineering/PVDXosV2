// Reads and patches the config/hpl_*_config.h files.
//
// These files use CMSIS Configuration Wizard comments. Every setting has a
// "// <id> key" line. The key is the same key that the .atstart file uses in a
// driver's "configuration" block. Option lines ("// <0x1=>Label" or
// "// <VALUE"> Label") map the label that the .atstart file stores to the C value.
import type { YamlValue } from './yaml.ts';

export interface ConfigOption {
    key: string;
    label: string;
}

export interface ConfigEntry {
    id: string;
    macro: string;
    line: number; // Index of the #define line.
    literal: string; // The C value, without a trailing comment.
    tag: string; // Wizard tag of the setting: o, q, e, y, s.
    title: string;
    help: string;
    options: ConfigOption[];
    range?: [number, number];
}

const TAG_RE = /^\/\/ <([oqeys])(?:\.\d+(?:\.\.\d+)?)?>\s*(.*)$/;
const OPT_NUM_RE = /^\/\/ <(0x[0-9a-fA-F]+|-?\d+)=>\s*(.*)$/;
const OPT_SYM_RE = /^\/\/ <([^"<>]+)">\s*(.*)$/;
const ID_RE = /^\/\/ <id>\s*(\S+)/;
const HELP_RE = /^\/\/ <i>\s*(.*)$/;
const DEFINE_RE = /^#define (\w+) (.*)$/;

export function parseConfigHeader(text: string): ConfigEntry[] {
    const lines = text.split('\n');
    const entries: ConfigEntry[] = [];
    let block: string[] = [];
    let pendingId: string | null = null;
    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        if (line.startsWith('//')) {
            block.push(line);
            const id = ID_RE.exec(line);
            if (id) pendingId = id[1];
            continue;
        }
        if (line.trim() === '') continue;
        const def = DEFINE_RE.exec(line);
        if (def && pendingId) {
            entries.push(describe(pendingId, def[1], i, def[2], block));
            pendingId = null;
        }
        if (!line.startsWith('#ifndef')) block = [];
    }
    return entries;
}

function describe(id: string, macro: string, line: number, value: string, block: string[]): ConfigEntry {
    const entry: ConfigEntry = { id, macro, line, literal: value.split(/\s+\/\//)[0].trim(), tag: '', title: '', help: '', options: [] };
    // Only the comment lines from the last wizard tag onwards belong to this setting.
    let start = 0;
    for (let i = block.length - 1; i >= 0; i--) {
        if (TAG_RE.test(block[i])) {
            start = i;
            break;
        }
    }
    for (const line of block.slice(start)) {
        let m: RegExpExecArray | null;
        if ((m = TAG_RE.exec(line))) {
            entry.tag = m[1];
            const range = /<(-?\w+)-(\w+)>\s*$/.exec(m[2]);
            if (range) {
                entry.range = [Number(range[1]), Number(range[2])];
                entry.title = m[2].slice(0, range.index).trim();
            } else {
                entry.title = m[2].trim();
            }
        } else if ((m = OPT_NUM_RE.exec(line)) || (m = OPT_SYM_RE.exec(line))) {
            entry.options.push({ key: m[1], label: m[2].trim() });
        } else if ((m = HELP_RE.exec(line))) {
            entry.help = m[1].trim();
        }
    }
    return entry;
}

function asNumber(literal: string): number | undefined {
    const s = literal.replace(/[uUlL]+$/, '');
    if (/^-?0x[0-9a-fA-F]+$/.test(s)) return parseInt(s, 16);
    if (/^-?\d+$/.test(s)) return parseInt(s, 10);
    return undefined;
}

// Converts an .atstart value into the C literal for this setting. Keeps the
// number style (hex or decimal) of the current literal. Returns undefined when
// the value cannot be expressed.
export function encodeValue(entry: ConfigEntry, value: YamlValue): string | undefined {
    if (entry.options.length > 0) {
        const option =
            entry.options.find((o) => o.label === String(value)) ??
            (typeof value === 'number' ? entry.options.find((o) => asNumber(o.key) === value) : undefined);
        if (!option) return undefined;
        // START writes numeric options in the number style of the default value.
        const n = asNumber(option.key);
        return n === undefined ? option.key : formatNumber(entry.literal, n);
    }
    if (typeof value === 'boolean') return formatNumber(entry.literal, value ? 1 : 0);
    if (typeof value === 'number' && Number.isInteger(value)) return formatNumber(entry.literal, value);
    return undefined;
}

function formatNumber(like: string, n: number): string {
    if (!/^-?0x/.test(like)) return String(n);
    const digits = n.toString(16);
    return '0x' + (/[A-F]/.test(like) ? digits.toUpperCase() : digits);
}

export function sameLiteral(a: string, b: string): boolean {
    const na = asNumber(a);
    const nb = asNumber(b);
    if (na !== undefined && nb !== undefined) return na === nb;
    return a === b;
}

// A setting can be edited when this tool can reproduce its current C value from
// its current .atstart value. Settings that Atmel START derived with a formula fail
// this check and stay read-only.
export function isEditable(entry: ConfigEntry, current: YamlValue): boolean {
    const encoded = encodeValue(entry, current);
    return encoded !== undefined && sameLiteral(encoded, entry.literal);
}

export function setDefine(text: string, entry: ConfigEntry, literal: string): string {
    const lines = text.split('\n');
    const old = lines[entry.line];
    const prefix = `#define ${entry.macro} `;
    if (!old.startsWith(prefix)) throw new Error(`config header changed under ${entry.macro}`);
    const rest = old.slice(prefix.length);
    lines[entry.line] = prefix + literal + rest.slice(rest.split(/\s+\/\//)[0].trimEnd().length);
    return lines.join('\n');
}

// Applies every changed setting of one driver instance to a config header.
// `owns` selects the macros of this instance, for example /^CONF_SERCOM_3_/.
export function patchSettings(
    text: string,
    owns: RegExp,
    before: Record<string, YamlValue>,
    after: Record<string, YamlValue>,
    where: string,
): string {
    const entries = parseConfigHeader(text).filter((e) => owns.test(e.macro));
    for (const [key, value] of Object.entries(after)) {
        if (JSON.stringify(before[key]) === JSON.stringify(value)) continue;
        const matches = entries.filter((e) => e.id === key);
        if (matches.length === 0) continue; // Not stored in this header.
        for (const entry of matches) {
            if (!isEditable(entry, before[key] ?? null)) {
                throw new Error(`${where}: setting "${key}" is derived by Atmel START and cannot be changed with this tool`);
            }
            const literal = encodeValue(entry, value);
            if (literal === undefined) throw new Error(`${where}: value ${JSON.stringify(value)} is not valid for "${key}"`);
            text = setDefine(text, entry, literal);
        }
    }
    return text;
}

// Sets a define that has no .atstart key, for example CONF_SERCOM_0_SPI_TXPO.
export function setMacro(text: string, macro: string, literal: string): string {
    const entry = parseConfigHeader(text).find((e) => e.macro === macro);
    if (entry) return setDefine(text, entry, literal);
    const re = new RegExp(`^#define ${macro} \\S+`, 'm');
    if (!re.test(text)) throw new Error(`macro ${macro} not found`);
    return text.replace(re, `#define ${macro} ${literal}`);
}

export function readMacro(text: string, macro: string): string | undefined {
    const m = new RegExp(`^#define ${macro} (\\S+)`, 'm').exec(text);
    return m?.[1];
}

// ---------------------------------------------------------------------------
// Instance blocks. hpl_sercom_config.h has one block for each configured SERCOM.
// Each block starts with "#include <peripheral_clk_config.h>". The blocks of
// peripheral_clk_config.h start with a "// <y> ... Clock Source" line.

export interface Block {
    key: string; // Instance key, for example SERCOM3.
    start: number; // First line.
    end: number; // One past the last line.
}

const END_MARK = '// <<< end of configuration section >>>';

export function sercomBlocks(text: string): Block[] {
    const lines = text.split('\n');
    const starts: number[] = [];
    lines.forEach((l, i) => {
        if (l === '#include <peripheral_clk_config.h>') starts.push(i);
    });
    const endMark = lines.indexOf(END_MARK);
    return starts.map((start, i) => {
        const end = i + 1 < starts.length ? starts[i + 1] : endMark;
        const m = /CONF_SERCOM_(\d+)_/.exec(lines.slice(start, end).join('\n'));
        return { key: `SERCOM${m?.[1] ?? '?'}`, start, end };
    });
}

export function clockBlocks(text: string): Block[] {
    const lines = text.split('\n');
    const segments: Block[] = [];
    let current: Block | null = null;
    for (let i = 0; i < lines.length; i++) {
        if (lines[i].startsWith('// <y>') || lines[i] === END_MARK) {
            if (current) segments.push(current);
            current = lines[i] === END_MARK ? null : { key: '', start: i, end: i };
            if (!current) break;
        }
        if (current) {
            current.end = i + 1;
            const m = /^#define CONF_GCLK_([A-Z]+\d*)_/.exec(lines[i]);
            if (m && !current.key) current.key = m[1];
        }
    }
    // Merge consecutive segments of the same instance (SERCOM has a core and a slow clock).
    const merged: Block[] = [];
    for (const s of segments) {
        const last = merged[merged.length - 1];
        if (last && last.key === s.key && last.end === s.start) last.end = s.end;
        else merged.push({ ...s });
    }
    return merged;
}

export function removeLines(text: string, block: Block): string {
    const lines = text.split('\n');
    lines.splice(block.start, block.end - block.start);
    return lines.join('\n');
}

export function insertLines(text: string, at: number, blockText: string): string {
    const lines = text.split('\n');
    const add = blockText.split('\n');
    if (add[add.length - 1] === '') add.pop();
    lines.splice(at, 0, ...add);
    return lines.join('\n');
}

export function endMarkLine(text: string): number {
    return text.split('\n').indexOf(END_MARK);
}

export function blockText(text: string, block: Block): string {
    return text.split('\n').slice(block.start, block.end).join('\n') + '\n';
}
