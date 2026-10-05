// In-memory model of an Atmel START project (atmel_start_config.atstart).
//
// The model edits the parsed YAML tree directly, so every field that this tool
// does not know about is kept as it is.
import { comparePins, findMux, isPin, muxOptions, PINS, RESERVED_PINS } from './device.ts';
import type { YamlMap, YamlValue } from './yaml.ts';

export type DriverKind = 'adc' | 'dac' | 'timer' | 'spi' | 'i2c' | 'delay' | 'pwm' | 'rand' | 'wdt' | 'system';

const KIND_BY_API: Record<string, DriverKind> = {
    'HAL:Driver:ADC_Sync': 'adc',
    'HAL:Driver:DAC_Sync': 'dac',
    'HAL:Driver:Timer': 'timer',
    'HAL:Driver:SPI_Master_Sync': 'spi',
    'HAL:Driver:I2C_Master_Sync': 'i2c',
    'HAL:Driver:Delay': 'delay',
    'HAL:Driver:PWM': 'pwm',
    'HAL:Driver:RAND_Sync': 'rand',
    'HAL:Driver:WDT': 'wdt',
};

export const KIND_NAMES: Record<DriverKind, string> = {
    adc: 'ADC (sync)',
    dac: 'DAC (sync)',
    timer: 'Timer (RTC)',
    spi: 'SPI master (sync)',
    i2c: 'I2C master (sync)',
    delay: 'Delay (SysTick)',
    pwm: 'PWM (TCC)',
    rand: 'Random (TRNG)',
    wdt: 'Watchdog',
    system: 'System',
};

export type PadMode = 'Digital output' | 'Digital input' | 'Analog' | 'I2C' | 'Peripheral IO';
export type Level = 'Low' | 'High';
export type Pull = 'Off' | 'Pull-up' | 'Pull-down';

export interface Driver {
    label: string;
    kind: DriverKind;
    instance: string; // Hardware instance, for example SERCOM3, ADC1, TCC0, RTC.
    index: number | null; // Instance number, for example 3 for SERCOM3.
    raw: YamlMap;
}

export interface Signal {
    driver: string;
    name: string; // For example SERCOM0/PAD/2 or ADC1/AIN/4.
    pin: string;
    role: string; // MOSI, SCK, MISO, SDA, SCL, or the optional signal label.
    peripheral: string;
    signal: string; // For example PAD2 or AIN4.
    required: boolean;
}

export interface Pad {
    label: string;
    pin: string;
    mode: string;
    level: Level;
    pull: Pull;
    raw: YamlMap;
}

export interface Issue {
    level: 'error' | 'warning';
    message: string;
    pins: string[];
    driver?: string;
}

const C_IDENT = /^[A-Za-z_][A-Za-z0-9_]*$/;

function clone<T>(v: T): T {
    return JSON.parse(JSON.stringify(v)) as T;
}

function asMap(v: YamlValue | undefined): YamlMap {
    return v !== null && typeof v === 'object' && !Array.isArray(v) ? v : {};
}

function asList(v: YamlValue | undefined): YamlMap[] {
    return Array.isArray(v) ? (v as YamlMap[]) : [];
}

export function definitionPart(definition: string, index: number): string {
    return definition.split('::')[index] ?? '';
}

export class Project {
    doc: YamlMap;
    readonly base: YamlMap;

    constructor(doc: YamlMap) {
        this.doc = doc;
        this.base = clone(doc);
    }

    get device(): string {
        return String(asMap(this.doc.board).device ?? '');
    }

    // Prefix of every definition string, for example "Atmel:SAMD51_Drivers:0.0.1::SAMD51P20A-AF".
    get definitionPrefix(): string {
        for (const d of Object.values(asMap(this.doc.drivers))) {
            const def = String(asMap(d).definition ?? '');
            if (def) return def.split('::').slice(0, 2).join('::');
        }
        return `Atmel:SAMD51_Drivers:0.0.1::${this.device}`;
    }

    // ---- Views ---------------------------------------------------------------

    drivers(): Driver[] {
        return Object.entries(asMap(this.doc.drivers)).map(([label, v]) => driverView(label, asMap(v)));
    }

    driver(label: string): Driver | undefined {
        const raw = asMap(this.doc.drivers)[label];
        return raw ? driverView(label, asMap(raw)) : undefined;
    }

    baseDriver(label: string): Driver | undefined {
        const raw = asMap(this.base.drivers)[label];
        return raw ? driverView(label, asMap(raw)) : undefined;
    }

    pads(): Pad[] {
        return Object.entries(asMap(this.doc.pads)).map(([label, v]) => padView(label, asMap(v)));
    }

    padAt(pin: string): Pad | undefined {
        return this.pads().find((p) => p.pin === pin);
    }

    pad(label: string): Pad | undefined {
        const raw = asMap(this.doc.pads)[label];
        return raw ? padView(label, asMap(raw)) : undefined;
    }

    signals(driverLabel?: string): Signal[] {
        const out: Signal[] = [];
        for (const d of this.drivers()) {
            if (driverLabel && d.label !== driverLabel) continue;
            for (const s of asList(asMap(d.raw.variant).required_signals)) {
                out.push(signalView(d.label, String(s.name), String(s.pad), String(s.label), true));
            }
            for (const s of asList(d.raw.optional_signals)) {
                if (s.mode === 'Disabled') continue;
                out.push(signalView(d.label, String(s.name), String(s.pad), String(s.label), false));
            }
        }
        return out;
    }

    signalAt(pin: string): Signal[] {
        return this.signals().filter((s) => s.pin === pin);
    }

    // Pads that no driver uses. These are set up as GPIO in system_init().
    gpioPads(): Pad[] {
        const used = new Set(this.signals().map((s) => s.pin));
        return this.pads()
            .filter((p) => !used.has(p.pin))
            .sort((a, b) => comparePins(a.pin, b.pin));
    }

    // ---- Pad edits -----------------------------------------------------------

    private padsMap(): YamlMap {
        if (!this.doc.pads || typeof this.doc.pads !== 'object') this.doc.pads = {};
        return this.doc.pads as YamlMap;
    }

    private newPadEntry(label: string, pin: string, mode: string): YamlMap {
        return {
            name: pin,
            definition: `${this.definitionPrefix}::pad::${pin}`,
            mode,
            user_label: label,
            configuration: null,
        };
    }

    // Creates or updates a plain GPIO pad.
    setGpio(pin: string, label: string, mode: 'Digital output' | 'Digital input', level: Level, pull: Pull): void {
        if (!isPin(pin)) throw new Error(`${pin} is not a pin of this device`);
        if (this.signalAt(pin).length > 0) throw new Error(`${pin} is used by a driver. Move or delete that signal first.`);
        const existing = this.padAt(pin);
        if (existing) this.renamePad(existing.label, label);
        const pads = this.padsMap();
        if (!pads[label]) pads[label] = this.newPadEntry(label, pin, mode);
        const target = pads[label] as YamlMap;
        target.mode = mode;
        const config: YamlMap = {};
        if (mode === 'Digital output' && level === 'High') config.pad_initial_level = 'High';
        if (mode === 'Digital input' && pull !== 'Off') config.pad_pull_config = pull;
        target.configuration = Object.keys(config).length ? config : null;
    }

    // Sets the initial level or pull of any pad, including driver pads (SPI MOSI/SCK level, MISO/I2C pull).
    setPadElectrical(label: string, level: Level, pull: Pull): void {
        const target = asMap(this.padsMap()[label]);
        if (!target.name) throw new Error(`no pad named ${label}`);
        const config: YamlMap = { ...asMap(target.configuration) };
        delete config.pad_initial_level;
        delete config.pad_pull_config;
        if (level === 'High') config.pad_initial_level = 'High';
        if (pull !== 'Off') config.pad_pull_config = pull;
        target.configuration = Object.keys(config).length ? config : null;
    }

    removePad(pin: string): void {
        if (this.signalAt(pin).length > 0) throw new Error(`${pin} is used by a driver. Delete the signal instead.`);
        const pad = this.padAt(pin);
        if (pad) delete this.padsMap()[pad.label];
    }

    renamePad(oldLabel: string, newLabel: string): void {
        if (oldLabel === newLabel) return;
        if (!C_IDENT.test(newLabel)) throw new Error(`"${newLabel}" is not a valid C identifier`);
        const pads = this.padsMap();
        if (pads[newLabel]) throw new Error(`a pad named ${newLabel} already exists`);
        const renamed: YamlMap = {};
        for (const [k, v] of Object.entries(pads)) {
            if (k === oldLabel) {
                const entry = asMap(v);
                entry.user_label = newLabel;
                renamed[newLabel] = entry;
            } else {
                renamed[k] = v;
            }
        }
        this.doc.pads = renamed;
    }

    // ---- Signal edits --------------------------------------------------------

    // Moves one driver signal (for example SPI MISO, or ADC AIN/4) to another pin.
    moveSignal(driverLabel: string, signalName: string, newPin: string): void {
        const d = this.driver(driverLabel);
        if (!d) throw new Error(`no driver ${driverLabel}`);
        const entry = this.signalEntry(d, signalName);
        if (!entry) throw new Error(`${driverLabel} has no signal ${signalName}`);
        const oldPin = String(entry.pad);
        if (oldPin === newPin) return;

        if (d.kind === 'spi' || d.kind === 'i2c') {
            const role = String(entry.label);
            const padNo = sercomPadFor(d, role, newPin, this.signals(driverLabel));
            if (padNo === undefined) throw new Error(`${newPin} cannot carry ${role} of ${d.instance}`);
            entry.name = `${d.instance}/PAD/${padNo}`;
        } else {
            // An ADC input, DAC output or TCC output has a fixed channel for each pin,
            // so a move can change the channel. Keep the channel if the new pin offers it.
            const used = new Set(this.signals(driverLabel).filter((s) => s.name !== signalName).map((s) => s.name));
            const choices = optionalSignalChoices(d, newPin).filter((n) => !used.has(n));
            const name = choices.includes(signalName) ? signalName : choices[0];
            if (!name) throw new Error(`${newPin} cannot carry a free ${signalName.split('/')[1]} channel of ${d.instance}`);
            this.setOptionalChannel(d, entry, name);
        }
        this.claimPin(newPin, oldPin, padModeFor(d.kind, String(entry.label)));
        entry.pad = newPin;
        if (d.kind === 'spi' || d.kind === 'i2c') this.normalizeSercom(d);
        else d.raw.optional_signals = asList(d.raw.optional_signals).sort((a, b) => signalSortKey(String(a.name)) - signalSortKey(String(b.name)));
    }

    private setOptionalChannel(d: Driver, entry: YamlMap, name: string): void {
        const [peripheral, group, num] = name.split('/');
        entry.name = name;
        entry.label = `${group}/${num}`;
        entry.identifier = `${d.label}:${group}/${num}`;
        entry.definition = `${this.definitionPrefix}::optional_signal_definition::${peripheral}.${group}.${num}`;
    }

    // Enables an optional signal (ADC input, TCC output, DAC output) on a pin.
    addOptionalSignal(driverLabel: string, signalName: string, pin: string, padLabel?: string): void {
        const d = this.driver(driverLabel);
        if (!d) throw new Error(`no driver ${driverLabel}`);
        if (d.kind !== 'adc' && d.kind !== 'pwm' && d.kind !== 'dac') throw new Error(`${KIND_NAMES[d.kind]} has no optional signals`);
        if (this.signalEntry(d, signalName)) throw new Error(`${signalName} is already enabled`);
        const [peripheral, group, num] = signalName.split('/');
        if (!findMux(pin, peripheral, group + num)) throw new Error(`${pin} cannot carry ${signalName}`);
        if (this.signalAt(pin).length > 0 || this.padAt(pin)) throw new Error(`${pin} is already in use`);
        if (padLabel !== undefined && !C_IDENT.test(padLabel)) throw new Error(`"${padLabel}" is not a valid C identifier`);
        const label = `${group}/${num}`;
        const entry: YamlMap = {
            identifier: `${driverLabel}:${label}`,
            pad: pin,
            mode: d.kind === 'pwm' ? 'PWM output' : 'Enabled',
            configuration: null,
            definition: `${this.definitionPrefix}::optional_signal_definition::${peripheral}.${group}.${num}`,
            name: signalName,
            label,
        };
        const list = asList(d.raw.optional_signals).slice();
        list.push(entry);
        list.sort((a, b) => signalSortKey(String(a.name)) - signalSortKey(String(b.name)));
        d.raw.optional_signals = list;
        this.padsMap()[padLabel ?? pin] = this.newPadEntry(padLabel ?? pin, pin, padModeFor(d.kind, label));
    }

    removeOptionalSignal(driverLabel: string, signalName: string): void {
        const d = this.driver(driverLabel);
        if (!d) throw new Error(`no driver ${driverLabel}`);
        const list = asList(d.raw.optional_signals);
        const entry = list.find((s) => s.name === signalName);
        if (!entry) throw new Error(`${signalName} is not enabled`);
        d.raw.optional_signals = list.filter((s) => s !== entry);
        const pad = this.padAt(String(entry.pad));
        if (pad) delete this.padsMap()[pad.label];
    }

    private signalEntry(d: Driver, signalName: string): YamlMap | undefined {
        return (
            asList(asMap(d.raw.variant).required_signals).find((s) => s.name === signalName) ??
            asList(d.raw.optional_signals).find((s) => s.name === signalName)
        );
    }

    // Moves the pad entry of a driver signal from oldPin to newPin.
    private claimPin(newPin: string, oldPin: string, mode: string): void {
        if (!isPin(newPin)) throw new Error(`${newPin} is not a pin of this device`);
        const occupant = this.padAt(newPin);
        if (occupant) throw new Error(`${newPin} is already used by pad ${occupant.label}. Free it first.`);
        const pad = this.padAt(oldPin);
        if (pad) {
            pad.raw.name = newPin;
            pad.raw.definition = `${this.definitionPrefix}::pad::${newPin}`;
            pad.raw.mode = mode;
            // A label that only repeats the old pin name follows the pin.
            if (pad.label === oldPin) this.renamePad(oldPin, newPin);
        } else {
            this.padsMap()[newPin] = this.newPadEntry(newPin, newPin, mode);
        }
    }

    // Recomputes the SERCOM pad order and the TXPO/RXPO specification.
    private normalizeSercom(d: Driver): void {
        const variant = asMap(d.raw.variant);
        const list = asList(variant.required_signals);
        list.sort((a, b) => sercomPadIndex(String(a.name)) - sercomPadIndex(String(b.name)));
        if (d.kind === 'spi') {
            const pads = Object.fromEntries(list.map((s) => [String(s.label), sercomPadIndex(String(s.name))]));
            const txpo = pads.MOSI === 3 ? 2 : 0;
            variant.specification = `TXPO=${txpo}, RXPO=${pads.MISO}`;
        }
    }

    // ---- Driver edits --------------------------------------------------------

    setDriverSetting(label: string, key: string, value: YamlValue): void {
        const d = this.driver(label);
        if (!d) throw new Error(`no driver ${label}`);
        const config = asMap(d.raw.configuration);
        if (!(key in config)) throw new Error(`${label} has no setting ${key}`);
        config[key] = value;
    }

    renameDriver(oldLabel: string, newLabel: string): void {
        if (oldLabel === newLabel) return;
        if (!C_IDENT.test(newLabel)) throw new Error(`"${newLabel}" is not a valid C identifier`);
        const drivers = asMap(this.doc.drivers);
        if (drivers[newLabel]) throw new Error(`a driver named ${newLabel} already exists`);
        const renamed: YamlMap = {};
        for (const [k, v] of Object.entries(drivers)) {
            if (k !== oldLabel) {
                renamed[k] = v;
                continue;
            }
            const entry = asMap(v);
            entry.user_label = newLabel;
            for (const s of asList(entry.optional_signals)) {
                s.identifier = String(s.identifier).replace(`${oldLabel}:`, `${newLabel}:`);
            }
            renamed[newLabel] = entry;
        }
        this.doc.drivers = renamed;
    }

    // Adds an SPI or I2C master on a free SERCOM. `pins` maps role (MOSI, SCK, MISO, SDA, SCL) to pin.
    addSercomDriver(
        kind: 'spi' | 'i2c',
        label: string,
        sercom: number,
        pins: Record<string, string>,
        template: YamlMap,
        padLabels: Record<string, string> = {},
    ): void {
        if (!C_IDENT.test(label)) throw new Error(`"${label}" is not a valid C identifier`);
        if (asMap(this.doc.drivers)[label]) throw new Error(`a driver named ${label} already exists`);
        const instance = `SERCOM${sercom}`;
        const busy = this.drivers().find((d) => d.instance === instance);
        if (busy) throw new Error(`${instance} is already used by ${busy.label}`);
        const roles = kind === 'spi' ? ['MOSI', 'SCK', 'MISO'] : ['SDA', 'SCL'];
        const entry = clone(template);
        entry.user_label = label;
        entry.definition = String(entry.definition).replace(/::SERCOM\d+::/, `::${instance}::`);
        const variant = asMap(entry.variant);
        const view = driverView(label, entry);
        const required: YamlMap[] = [];
        const chosen: Signal[] = [];
        for (const role of roles) {
            const pin = pins[role];
            if (!pin) throw new Error(`choose a pin for ${role}`);
            const padNo = sercomPadFor(view, role, pin, chosen);
            if (padNo === undefined) throw new Error(`${pin} cannot carry ${role} of ${instance}`);
            const s: YamlMap = { name: `${instance}/PAD/${padNo}`, pad: pin, label: role };
            required.push(s);
            chosen.push(signalView(label, String(s.name), pin, role, true));
        }
        for (const role of roles) {
            const pin = pins[role];
            if (this.padAt(pin) || this.signalAt(pin).length) throw new Error(`${pin} is already in use`);
        }
        variant.required_signals = required;
        entry.variant = variant;

        // Atmel START keeps the drivers sorted by hardware instance name.
        const drivers = asMap(this.doc.drivers);
        const ordered: YamlMap = {};
        let inserted = false;
        for (const [k, v] of Object.entries(drivers)) {
            const inst = definitionPart(String(asMap(v).definition), 2);
            if (!inserted && inst > instance) {
                ordered[label] = entry;
                inserted = true;
            }
            ordered[k] = v;
        }
        if (!inserted) ordered[label] = entry;
        this.doc.drivers = ordered;
        this.normalizeSercom(driverView(label, entry));

        for (const role of roles) {
            const padLabel = padLabels[role] || `${label}_${role}`.replace(/^(SPI|I2C)_/, '');
            if (!C_IDENT.test(padLabel)) throw new Error(`"${padLabel}" is not a valid C identifier`);
            if (this.padsMap()[padLabel]) throw new Error(`a pad named ${padLabel} already exists`);
            this.padsMap()[padLabel] = this.newPadEntry(padLabel, pins[role], padModeFor(kind, role));
        }
    }

    removeDriver(label: string): void {
        const d = this.driver(label);
        if (!d) throw new Error(`no driver ${label}`);
        if (d.kind !== 'spi' && d.kind !== 'i2c') throw new Error('Only SPI and I2C drivers can be deleted in this version');
        for (const s of this.signals(label)) {
            const pad = this.padAt(s.pin);
            if (pad) delete this.padsMap()[pad.label];
        }
        delete asMap(this.doc.drivers)[label];
    }

    // ---- Checks --------------------------------------------------------------

    validate(): Issue[] {
        const issues: Issue[] = [];
        const err = (message: string, pins: string[] = [], driver?: string) => issues.push({ level: 'error', message, pins, driver });
        const warn = (message: string, pins: string[] = [], driver?: string) => issues.push({ level: 'warning', message, pins, driver });

        const padsByPin = new Map<string, string[]>();

        const labels = new Map<string, string>();
        for (const p of this.pads()) {
            if (!isPin(p.pin)) err(`Pad ${p.label} uses ${p.pin}, which this device does not have`, [p.pin]);
            if (!C_IDENT.test(p.label)) err(`Pad label "${p.label}" is not a valid C identifier`, [p.pin]);
            if (p.raw.user_label !== p.label) err(`Pad ${p.label} has a different user_label (${String(p.raw.user_label)})`, [p.pin]);
            padsByPin.set(p.pin, [...(padsByPin.get(p.pin) ?? []), p.label]);
            labels.set(p.label, `pad on ${p.pin}`);
            if (RESERVED_PINS[p.pin]) warn(`${p.pin} is the ${RESERVED_PINS[p.pin]} debug pin. Using it disables the debugger.`, [p.pin]);
        }
        for (const [pin, names] of padsByPin) {
            if (names.length > 1) err(`${pin} has more than one pad: ${names.join(', ')}`, [pin]);
        }
        for (const d of this.drivers()) {
            if (labels.has(d.label)) err(`Driver ${d.label} has the same name as a ${labels.get(d.label)}`, [], d.label);
        }

        const instances = new Map<string, string>();
        for (const d of this.drivers()) {
            if (d.kind === 'system') continue;
            const other = instances.get(d.instance);
            if (other) err(`${d.instance} is used by both ${other} and ${d.label}`, [], d.label);
            instances.set(d.instance, d.label);
        }

        for (const s of this.signals()) {
            const usedBy = `${s.driver} ${s.role}`;
            const pinsOfSignal = this.signals().filter((o) => o.pin === s.pin && o !== s);
            for (const o of pinsOfSignal) {
                if (o.driver < s.driver || (o.driver === s.driver && o.name < s.name)) {
                    err(`${s.pin} is used by both ${o.driver} ${o.role} and ${usedBy}`, [s.pin], s.driver);
                }
            }
            if (!findMux(s.pin, s.peripheral, s.signal)) {
                err(`${s.pin} cannot carry ${s.peripheral} ${s.signal} (${usedBy})`, [s.pin], s.driver);
            }
            const pad = this.padAt(s.pin);
            if (!pad) err(`${usedBy} on ${s.pin} has no pad entry`, [s.pin], s.driver);
        }

        for (const p of this.gpioPads()) {
            if (p.mode !== 'Digital output' && p.mode !== 'Digital input') {
                err(`Pad ${p.label} on ${p.pin} has mode "${p.mode}" but no driver uses it`, [p.pin]);
            }
        }

        for (const d of this.drivers()) {
            if (d.kind !== 'spi' && d.kind !== 'i2c') continue;
            const sigs = this.signals(d.label);
            const pad = (role: string) => {
                const s = sigs.find((x) => x.role === role);
                return s ? sercomPadIndex(s.name) : undefined;
            };
            if (d.kind === 'spi') {
                const mosi = pad('MOSI');
                const sck = pad('SCK');
                const miso = pad('MISO');
                if (sck !== 1) err(`${d.label}: SCK must be on ${d.instance} PAD1`, [], d.label);
                if (mosi !== 0 && mosi !== 3) err(`${d.label}: MOSI must be on ${d.instance} PAD0 or PAD3`, [], d.label);
                if (miso === undefined || miso === mosi || miso === sck) err(`${d.label}: MISO must be on a free pad of ${d.instance}`, [], d.label);
                const spec = String(asMap(d.raw.variant).specification);
                const want = `TXPO=${mosi === 3 ? 2 : 0}, RXPO=${miso}`;
                if (spec !== want) err(`${d.label}: pad layout "${spec}" does not match the pins (${want})`, [], d.label);
            } else {
                if (pad('SDA') !== 0) err(`${d.label}: SDA must be on ${d.instance} PAD0`, [], d.label);
                if (pad('SCL') !== 1) err(`${d.label}: SCL must be on ${d.instance} PAD1`, [], d.label);
            }
        }
        return issues;
    }
}

export function driverView(label: string, raw: YamlMap): Driver {
    const definition = String(raw.definition ?? '');
    const instance = definitionPart(definition, 2);
    const api = String(raw.api ?? '');
    let kind: DriverKind = KIND_BY_API[api] ?? 'system';
    if (kind === 'system' && !api.startsWith('HAL:HPL:')) {
        throw new Error(`Driver ${label} uses ${api}, which this tool does not support yet`);
    }
    if (kind === 'pwm' && !instance.startsWith('TCC')) throw new Error(`Driver ${label}: PWM on ${instance} is not supported yet`);
    if (kind === 'timer' && instance !== 'RTC') throw new Error(`Driver ${label}: Timer on ${instance} is not supported yet`);
    const num = /(\d+)$/.exec(instance);
    return { label, kind, instance, index: num ? parseInt(num[1], 10) : null, raw };
}

function padView(label: string, raw: YamlMap): Pad {
    const config = asMap(raw.configuration);
    return {
        label,
        pin: String(raw.name),
        mode: String(raw.mode),
        level: config.pad_initial_level === 'High' ? 'High' : 'Low',
        pull: (config.pad_pull_config as Pull) ?? 'Off',
        raw,
    };
}

function signalView(driver: string, name: string, pin: string, role: string, required: boolean): Signal {
    const [peripheral, group, num] = name.split('/');
    return { driver, name, pin, role, peripheral, signal: `${group}${num}`, required };
}

export function sercomPadIndex(name: string): number {
    return parseInt(name.split('/')[2], 10);
}

function signalSortKey(name: string): number {
    return parseInt(name.split('/')[2] ?? '0', 10);
}

export function padModeFor(kind: DriverKind, role: string): PadMode {
    switch (kind) {
        case 'spi':
            return role === 'MISO' ? 'Digital input' : 'Digital output';
        case 'i2c':
            return 'I2C';
        case 'adc':
        case 'dac':
            return 'Analog';
        default:
            return 'Peripheral IO';
    }
}

// Legal SERCOM pads for each role, given the pads the other roles already use.
export function sercomPadChoices(kind: 'spi' | 'i2c', role: string): number[] {
    if (kind === 'i2c') return role === 'SDA' ? [0] : [1];
    if (role === 'SCK') return [1];
    if (role === 'MOSI') return [0, 3];
    return [0, 2, 3];
}

function sercomPadFor(d: Driver, role: string, pin: string, others: Signal[]): number | undefined {
    if (d.kind !== 'spi' && d.kind !== 'i2c') return undefined;
    const taken = new Set(others.filter((s) => s.role !== role).map((s) => sercomPadIndex(s.name)));
    for (const padNo of sercomPadChoices(d.kind, role)) {
        if (taken.has(padNo)) continue;
        if (findMux(pin, d.instance, `PAD${padNo}`)) return padNo;
    }
    return undefined;
}

// Pins that can carry a SERCOM role, with the pad each would use.
export function sercomPinChoices(kind: 'spi' | 'i2c', sercom: string, role: string): { pin: string; pad: number }[] {
    const out: { pin: string; pad: number }[] = [];
    for (const padNo of sercomPadChoices(kind, role)) {
        for (const pin of PINS) {
            if (findMux(pin, sercom, `PAD${padNo}`)) out.push({ pin, pad: padNo });
        }
    }
    return out.sort((a, b) => comparePins(a.pin, b.pin));
}

// Optional signal names (for example ADC1/AIN/5) that a pin can carry for a driver.
export function optionalSignalChoices(d: Driver, pin: string): string[] {
    const prefix = d.kind === 'adc' ? 'AIN' : d.kind === 'pwm' ? 'WO' : d.kind === 'dac' ? 'VOUT' : '';
    if (!prefix) return [];
    return muxOptions(pin)
        .filter((m) => m.peripheral === d.instance && m.signal.startsWith(prefix))
        .map((m) => `${d.instance}/${prefix}/${m.signal.slice(prefix.length)}`);
}
