// Describes the editable settings of a driver for the user interface.
import { readText, type Atzip } from './atzip.ts';
import { isEditable, parseConfigHeader, type ConfigEntry } from './configh.ts';
import { baseCounterpart, headerRule, templates } from './generate.ts';
import type { Driver, Project } from './project.ts';
import type { YamlMap, YamlValue } from './yaml.ts';

export interface SettingField {
    key: string;
    title: string;
    help: string;
    value: YamlValue;
    kind: 'select' | 'bool' | 'number' | 'text';
    options: string[];
    range?: [number, number];
    editable: boolean;
    reason?: string; // Why the field is read-only.
}

function fill(text: string, n: number): string {
    return text.replace(/\{\{N\}\}/g, String(n));
}

export function settingFields(zip: Atzip, project: Project, d: Driver): SettingField[] {
    const config = (d.raw.configuration ?? {}) as YamlMap;
    const rule = headerRule(d);
    const counterpart = baseCounterpart(project, d);
    let entries: ConfigEntry[] = [];
    let before: YamlMap = {};
    if (rule && zip.files.has(rule.file)) {
        if (counterpart) {
            entries = parseConfigHeader(readText(zip, rule.file));
            before = (counterpart.raw.configuration ?? {}) as YamlMap;
        } else if (d.kind === 'spi' || d.kind === 'i2c') {
            const tpl = d.kind === 'spi' ? templates.sercomSpiBlock : templates.sercomI2cBlock;
            entries = parseConfigHeader(fill(tpl, d.index ?? 0));
            before = (d.kind === 'spi' ? templates.spiDriver.configuration : templates.i2cDriver.configuration) as YamlMap;
        }
        entries = entries.filter((e) => rule.owns(d).test(e.macro));
    }

    return Object.keys(config).map((key) => {
        const value = config[key];
        const entry = entries.find((e) => e.id === key);
        const field: SettingField = {
            key,
            title: entry?.title || key,
            help: entry?.help ?? '',
            value,
            kind: 'text',
            options: [],
            editable: false,
        };
        if (!entry) {
            field.reason = 'Not stored in a config header';
            return field;
        }
        if (entry.options.length > 0) {
            field.kind = 'select';
            field.options = entry.options.map((o) => o.label);
        } else if (typeof value === 'boolean') {
            field.kind = 'bool';
        } else if (typeof value === 'number') {
            field.kind = 'number';
            field.range = entry.range;
        }
        field.editable = isEditable(entry, before[key] ?? null);
        if (!field.editable) field.reason = 'Atmel START derived this value with a formula, so it is read-only here';
        return field;
    });
}
