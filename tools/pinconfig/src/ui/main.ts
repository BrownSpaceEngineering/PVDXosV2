import './style.css';
import { readAtzip, readText, writeAtzip, type Atzip } from '../core/atzip.ts';
import { comparePins, muxOptions, PINS, RESERVED_PINS } from '../core/device.ts';
import { generate, loadProject, templates, type Generated } from '../core/generate.ts';
import { markdownPinTables, pinUse } from '../core/pintable.ts';
import {
    KIND_NAMES,
    optionalSignalChoices,
    sercomPinChoices,
    type Driver,
    type Issue,
    type Level,
    type Project,
    type Pull,
    type Signal,
} from '../core/project.ts';
import { settingFields } from '../core/settings.ts';
import type { YamlMap, YamlValue } from '../core/yaml.ts';
import { diffLines, hunks } from './diff.ts';

// ---- State -------------------------------------------------------------------

type Tab = 'pins' | 'drivers' | 'changes' | 'problems';

interface State {
    zip: Atzip | null;
    fileName: string;
    project: Project | null;
    undo: string[];
    redo: string[];
    tab: Tab;
    pin: string | null;
    driver: string | null;
    file: string | null;
    filter: string;
    usedOnly: boolean;
    adding: boolean;
}

const state: State = {
    zip: null,
    fileName: '',
    project: null,
    undo: [],
    redo: [],
    tab: 'pins',
    pin: null,
    driver: null,
    file: null,
    filter: '',
    usedOnly: false,
    adding: false,
};

const README_VIEW = 'README pin tables (Markdown)';

// ---- DOM helper ----------------------------------------------------------------

type Child = Node | string | null | undefined | false;

function h<K extends keyof HTMLElementTagNameMap>(
    tag: K,
    attrs: Record<string, unknown> = {},
    ...children: (Child | Child[])[]
): HTMLElementTagNameMap[K] {
    const el = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
        if (v === undefined || v === null || v === false) continue;
        if (k.startsWith('on') && typeof v === 'function') el.addEventListener(k.slice(2), v as EventListener);
        else if (k === 'class') el.className = String(v);
        else if (k === 'value') (el as HTMLInputElement).value = String(v);
        else if (k === 'checked') (el as HTMLInputElement).checked = Boolean(v);
        else el.setAttribute(k, v === true ? '' : String(v));
    }
    for (const c of children.flat()) {
        if (c === null || c === undefined || c === false) continue;
        el.append(typeof c === 'string' ? document.createTextNode(c) : c);
    }
    return el;
}

function select(value: string, options: (string | [string, string])[], onChange: (v: string) => void, attrs: Record<string, unknown> = {}) {
    return h(
        'select',
        { ...attrs, onchange: (e: Event) => onChange((e.target as HTMLSelectElement).value) },
        options.map((o) => {
            const [v, label] = Array.isArray(o) ? o : [o, o];
            return h('option', { value: v, selected: v === value }, label);
        }),
    );
}

function textInput(value: string, onCommit: (v: string) => void, attrs: Record<string, unknown> = {}) {
    const commit = (e: Event) => {
        const v = (e.target as HTMLInputElement).value.trim();
        if (v !== value) onCommit(v);
    };
    return h('input', {
        type: 'text',
        value,
        spellcheck: 'false',
        ...attrs,
        onchange: commit,
        onkeydown: (e: KeyboardEvent) => {
            if (e.key === 'Enter') (e.target as HTMLInputElement).blur();
        },
    });
}

let toastTimer = 0;
function toast(message: string, kind: 'error' | 'info' = 'error') {
    document.querySelector('.toast')?.remove();
    const el = h('div', { class: `toast ${kind}`, role: 'status', title: 'Click to close', onclick: () => el.remove() }, message);
    document.body.append(el);
    clearTimeout(toastTimer);
    toastTimer = window.setTimeout(() => el.remove(), kind === 'error' ? 7000 : 3500);
}

// ---- Edits and history -----------------------------------------------------------

function edit(fn: (p: Project) => void): void {
    const p = state.project;
    if (!p) return;
    const snapshot = JSON.stringify(p.doc);
    try {
        fn(p);
        state.undo.push(snapshot);
        state.redo = [];
    } catch (e) {
        p.doc = JSON.parse(snapshot) as YamlMap;
        toast((e as Error).message);
    }
    render();
}

function undo(): void {
    const p = state.project;
    const prev = state.undo.pop();
    if (!p || prev === undefined) return;
    state.redo.push(JSON.stringify(p.doc));
    p.doc = JSON.parse(prev) as YamlMap;
    render();
}

function redo(): void {
    const p = state.project;
    const next = state.redo.pop();
    if (!p || next === undefined) return;
    state.undo.push(JSON.stringify(p.doc));
    p.doc = JSON.parse(next) as YamlMap;
    render();
}

// ---- Loading and saving --------------------------------------------------------

async function openFile(file: File): Promise<void> {
    try {
        const zip = await readAtzip(new Uint8Array(await file.arrayBuffer()));
        const project = loadProject(zip);
        project.drivers(); // Fails early on driver types this tool does not support.
        Object.assign(state, { zip, project, fileName: file.name, undo: [], redo: [], pin: null, driver: null, file: null });
        const check = tryGenerate();
        if ('error' in check) toast(`Loaded, but the project has problems:\n${check.error}`);
        else if (check.out.changed.length > 0) {
            toast(`Warning: this tool does not reproduce ${check.out.changed.join(', ')} exactly. Review the Changes tab before you export.`);
        } else toast(`Loaded ${file.name}. The tool reproduces every generated file exactly.`, 'info');
    } catch (e) {
        toast(`Could not open ${file.name}: ${(e as Error).message}`);
    }
    render();
}

function tryGenerate(): { out: Generated } | { error: string } {
    try {
        return { out: generate(state.zip!, state.project!) };
    } catch (e) {
        return { error: (e as Error).message };
    }
}

async function download(): Promise<void> {
    const result = tryGenerate();
    if ('error' in result) {
        state.tab = 'problems';
        render();
        toast(result.error);
        return;
    }
    const data = await writeAtzip(state.zip!, result.out.files);
    const blob = new Blob([data as BlobPart], { type: 'application/zip' });
    const a = h('a', { href: URL.createObjectURL(blob), download: state.fileName || 'config.atzip' });
    document.body.append(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 10_000);
    toast(`Saved ${state.fileName}. Changed files: ${result.out.changed.length ? result.out.changed.join(', ') : 'none'}.`, 'info');
}

function pickFile(): void {
    const input = h('input', { type: 'file', accept: '.atzip,.zip' });
    input.addEventListener('change', () => {
        const f = input.files?.[0];
        if (f) void openFile(f);
    });
    input.click();
}

// ---- Rendering -----------------------------------------------------------------

const app = document.getElementById('app')!;

function render(): void {
    const scroller = app.querySelector('.table-wrap');
    const scrollTop = scroller?.scrollTop ?? 0;
    app.replaceChildren(header(), ...(state.project ? [tabs(), h('main', {}, body())] : [h('main', {}, dropZone())]));
    const next = app.querySelector('.table-wrap');
    if (next) next.scrollTop = scrollTop;
}

function issues(): Issue[] {
    return state.project?.validate() ?? [];
}

function header(): HTMLElement {
    const loaded = state.project !== null;
    const errs = issues().filter((i) => i.level === 'error').length;
    return h(
        'header',
        { class: 'bar' },
        h('h1', {}, 'PVDX Pin Configurator'),
        loaded && h('span', { class: 'file' }, `${state.fileName} · ${state.project!.device}`),
        h('span', { class: 'spacer' }),
        h('button', { onclick: pickFile }, loaded ? 'Open another .atzip' : 'Open .atzip'),
        loaded && h('button', { onclick: undo, disabled: state.undo.length === 0, title: 'Undo (Ctrl+Z)' }, 'Undo'),
        loaded && h('button', { onclick: redo, disabled: state.redo.length === 0, title: 'Redo (Ctrl+Shift+Z)' }, 'Redo'),
        loaded &&
            h(
                'button',
                { class: 'primary', onclick: () => void download(), disabled: errs > 0, title: errs ? 'Fix the problems first' : undefined },
                'Download .atzip',
            ),
    );
}

function tabs(): HTMLElement {
    const all = issues();
    const errs = all.filter((i) => i.level === 'error').length;
    const warns = all.length - errs;
    const tab = (id: Tab, label: string, extra?: Child) =>
        h('button', { class: state.tab === id ? 'active' : '', onclick: () => ((state.tab = id), render()) }, label, extra);
    return h(
        'nav',
        { class: 'tabs' },
        tab('pins', 'Pins'),
        tab('drivers', 'Drivers'),
        tab('changes', 'Changes', state.undo.length ? h('span', { class: 'badge ok' }, '●') : null),
        tab(
            'problems',
            'Problems',
            errs ? h('span', { class: 'badge err' }, String(errs)) : warns ? h('span', { class: 'badge warn' }, String(warns)) : null,
        ),
    );
}

function body(): HTMLElement {
    switch (state.tab) {
        case 'pins':
            return pinsView();
        case 'drivers':
            return driversView();
        case 'changes':
            return changesView();
        case 'problems':
            return problemsView();
    }
}

function dropZone(): HTMLElement {
    const zone = h(
        'div',
        { class: 'drop' },
        h('h2', {}, 'Open an Atmel START .atzip'),
        h(
            'p',
            {},
            'Drop the .atzip from the PVDX-SAMD-PinConfig submodule here, or choose it with the button. ',
            'Everything runs in this browser tab. Nothing is uploaded.',
        ),
        h('button', { class: 'primary', onclick: pickFile }, 'Choose .atzip'),
    );
    zone.addEventListener('dragover', (e) => {
        e.preventDefault();
        zone.classList.add('over');
    });
    zone.addEventListener('dragleave', () => zone.classList.remove('over'));
    zone.addEventListener('drop', (e) => {
        e.preventDefault();
        zone.classList.remove('over');
        const f = e.dataTransfer?.files[0];
        if (f) void openFile(f);
    });
    return zone;
}

// ---- Pins view -------------------------------------------------------------------

function pinsView(): HTMLElement {
    const p = state.project!;
    const pinIssues = new Map<string, 'error' | 'warning'>();
    for (const i of issues()) for (const pin of i.pins) if (pinIssues.get(pin) !== 'error') pinIssues.set(pin, i.level);
    const q = state.filter.toLowerCase();
    const rows = [...PINS].sort(comparePins).flatMap((pin) => {
        const pad = p.padAt(pin);
        const use = pinUse(p, pin);
        const used = Boolean(pad) || p.signalAt(pin).length > 0;
        if (state.usedOnly && !used) return [];
        const hay = `${pin} ${pad?.label ?? ''} ${use} ${muxOptions(pin)
            .map((m) => `${m.peripheral} ${m.signal}`)
            .join(' ')}`.toLowerCase();
        if (q && !hay.includes(q)) return [];
        const cls = ['row', used ? '' : 'free', state.pin === pin ? 'selected' : '', pinIssues.get(pin) ?? ''].join(' ');
        return [
            h(
                'tr',
                { class: cls, onclick: () => ((state.pin = pin), render()) },
                h('td', { class: 'mono' }, pin),
                h('td', { class: 'mono' }, pad?.label ?? ''),
                h('td', {}, use || (RESERVED_PINS[pin] ? '' : 'Free')),
            ),
        ];
    });
    return h(
        'div',
        { class: 'split' },
        h(
            'section',
            { class: 'panel' },
            h(
                'div',
                { class: 'toolbar' },
                h('input', {
                    type: 'search',
                    placeholder: 'Filter by pin, label, use or peripheral (for example SERCOM5)',
                    value: state.filter,
                    oninput: (e: Event) => {
                        state.filter = (e.target as HTMLInputElement).value;
                        const pos = (e.target as HTMLInputElement).selectionStart;
                        render();
                        const el = app.querySelector<HTMLInputElement>('input[type=search]');
                        el?.focus();
                        el?.setSelectionRange(pos, pos);
                    },
                }),
                h(
                    'label',
                    {},
                    h('input', { type: 'checkbox', checked: state.usedOnly, onchange: () => ((state.usedOnly = !state.usedOnly), render()) }),
                    ' Used pins only',
                ),
            ),
            h(
                'div',
                { class: 'table-wrap' },
                h('table', {}, h('thead', {}, h('tr', {}, h('th', {}, 'Pin'), h('th', {}, 'Label'), h('th', {}, 'Use'))), h('tbody', {}, rows)),
            ),
        ),
        h('aside', { class: 'panel sticky' }, state.pin ? pinInspector(state.pin) : h('p', { class: 'note' }, 'Select a pin to edit it.')),
    );
}

const LEVELS: Level[] = ['Low', 'High'];
const PULLS: Pull[] = ['Off', 'Pull-up', 'Pull-down'];

function pinInspector(pin: string): HTMLElement {
    const p = state.project!;
    const pad = p.padAt(pin);
    const signals = p.signalAt(pin);
    const parts: Child[] = [h('h2', {}, pin, pad ? ` · ${pad.label}` : '')];

    if (RESERVED_PINS[pin]) parts.push(h('p', { class: 'note' }, `${pin} is the ${RESERVED_PINS[pin]} pin of the SWD debug port. Leave it free.`));

    for (const issue of issues().filter((i) => i.pins.includes(pin))) {
        parts.push(h('div', { class: `issue ${issue.level}` }, issue.message));
    }

    if (signals.length > 0) {
        for (const s of signals) parts.push(signalEditor(s));
    } else if (pad) {
        parts.push(gpioEditor(pin));
    } else {
        parts.push(freePinEditor(pin));
    }

    parts.push(h('h3', {}, 'Possible functions of this pin'), muxTable(pin));
    return h('div', {}, parts);
}

function labelRow(label: string): HTMLElement[] {
    return [
        h('label', {}, 'Label'),
        textInput(label, (v) => edit((p) => p.renamePad(label, v)), { class: 'mono', 'aria-label': 'Pad label' }),
        h('div', { class: 'field-help' }, 'This becomes the C macro in atmel_start_pins.h. Rename the uses in the firmware too.'),
    ];
}

function gpioEditor(pin: string): HTMLElement {
    const pad = state.project!.padAt(pin)!;
    const set = (mode: 'Digital output' | 'Digital input', level: Level, pull: Pull) =>
        edit((p) => p.setGpio(pin, pad.label, mode, level, pull));
    const isOut = pad.mode !== 'Digital input';
    return h(
        'div',
        {},
        h(
            'div',
            { class: 'kv' },
            labelRow(pad.label),
            h('label', {}, 'Direction'),
            select(pad.mode, ['Digital output', 'Digital input'], (v) => set(v as 'Digital output', pad.level, pad.pull)),
            isOut && h('label', {}, 'Initial level'),
            isOut && select(pad.level, LEVELS, (v) => set('Digital output', v as Level, pad.pull)),
            !isOut && h('label', {}, 'Pull'),
            !isOut && select(pad.pull, PULLS, (v) => set('Digital input', pad.level, v as Pull)),
        ),
        h('div', { class: 'row-actions' }, h('button', { class: 'danger', onclick: () => edit((p) => p.removePad(pin)) }, 'Free this pin')),
    );
}

function freePinEditor(pin: string): HTMLElement {
    const p = state.project!;
    const form = { label: pin, mode: 'Digital output' as 'Digital output' | 'Digital input', level: 'Low' as Level, pull: 'Off' as Pull };
    const enable: HTMLElement[] = [];
    for (const d of p.drivers()) {
        const used = new Set(p.signals(d.label).map((s) => s.name));
        for (const name of optionalSignalChoices(d, pin)) {
            if (used.has(name)) continue;
            enable.push(
                h(
                    'button',
                    { onclick: () => edit((pr) => pr.addOptionalSignal(d.label, name, pin, form.label === pin ? undefined : form.label)) },
                    `Add ${d.label} ${name.split('/').slice(1).join('/')}`,
                ),
            );
        }
    }
    return h(
        'div',
        {},
        h('p', { class: 'note' }, 'This pin is free.'),
        h('h3', {}, 'Use as GPIO'),
        h(
            'div',
            { class: 'kv' },
            h('label', {}, 'Label'),
            h('input', { type: 'text', class: 'mono', value: form.label, spellcheck: 'false', oninput: (e: Event) => (form.label = (e.target as HTMLInputElement).value.trim()) }),
            h('label', {}, 'Direction'),
            select(form.mode, ['Digital output', 'Digital input'], (v) => (form.mode = v as typeof form.mode)),
            h('label', {}, 'Initial level'),
            select(form.level, LEVELS, (v) => (form.level = v as Level)),
            h('label', {}, 'Pull (inputs)'),
            select(form.pull, PULLS, (v) => (form.pull = v as Pull)),
        ),
        h('div', { class: 'row-actions' }, h('button', { class: 'primary', onclick: () => edit((pr) => pr.setGpio(pin, form.label, form.mode, form.level, form.pull)) }, 'Create GPIO')),
        enable.length > 0 && h('h3', {}, 'Or give it to a driver'),
        enable.length > 0 && h('p', { class: 'note' }, 'The label field above also names the new pad.'),
        enable.length > 0 && h('div', { class: 'row-actions' }, enable),
    );
}

// Pins that a signal can move to: legal for the signal and not used by anything else.
function moveTargets(d: Driver, s: Signal): string[] {
    const p = state.project!;
    const free = (pin: string) => pin === s.pin || (!p.padAt(pin) && p.signalAt(pin).length === 0);
    let pins: string[];
    if (d.kind === 'spi' || d.kind === 'i2c') {
        pins = sercomPinChoices(d.kind, d.instance, s.role).map((c) => c.pin);
    } else {
        const used = new Set(p.signals(d.label).filter((o) => o.name !== s.name).map((o) => o.name));
        pins = PINS.filter((pin) => optionalSignalChoices(d, pin).some((n) => !used.has(n)));
    }
    return [...new Set(pins)].filter(free).sort(comparePins);
}

function signalEditor(s: Signal): HTMLElement {
    const p = state.project!;
    const d = p.driver(s.driver)!;
    const pad = p.padAt(s.pin);
    const targets = moveTargets(d, s);
    const isOutput = d.kind === 'spi' && s.role !== 'MISO';
    const hasPull = (d.kind === 'spi' && s.role === 'MISO') || d.kind === 'i2c';
    return h(
        'div',
        {},
        h(
            'div',
            { class: 'kv' },
            h('label', {}, 'Driver'),
            h('span', {}, h('a', { href: '#', onclick: (e: Event) => (e.preventDefault(), (state.tab = 'drivers'), (state.driver = d.label), render()) }, d.label), ` · ${KIND_NAMES[d.kind]} on ${d.instance}`),
            h('label', {}, 'Signal'),
            h('span', { class: 'mono' }, `${s.role} (${s.peripheral} ${s.signal})`),
            pad && labelRow(pad.label),
            h('label', {}, 'Move to pin'),
            select(s.pin, targets, (v) => edit((pr) => pr.moveSignal(d.label, s.name, v)), { class: 'mono' }),
            d.kind !== 'spi' && d.kind !== 'i2c' && h('div', { class: 'field-help' }, 'Each pin has a fixed channel. A move can change the channel number that the firmware uses.'),
            isOutput && pad && h('label', {}, 'Initial level'),
            isOutput && pad && select(pad.level, LEVELS, (v) => edit((pr) => pr.setPadElectrical(pad.label, v as Level, pad.pull))),
            hasPull && pad && h('label', {}, 'Pull'),
            hasPull && pad && select(pad.pull, PULLS, (v) => edit((pr) => pr.setPadElectrical(pad.label, pad.level, v as Pull))),
        ),
        !s.required &&
            h('div', { class: 'row-actions' }, h('button', { class: 'danger', onclick: () => edit((pr) => pr.removeOptionalSignal(d.label, s.name)) }, `Disable ${s.role}`)),
    );
}

function muxTable(pin: string): HTMLElement {
    const p = state.project!;
    const current = new Set(p.signalAt(pin).map((s) => `${s.peripheral}_${s.signal}`));
    const opts = muxOptions(pin);
    if (opts.length === 0) return h('p', { class: 'note' }, 'GPIO only.');
    return h(
        'table',
        { class: 'muxlist' },
        h('thead', {}, h('tr', {}, h('th', {}, 'Fn'), h('th', {}, 'Peripheral'), h('th', {}, 'Signal'))),
        h(
            'tbody',
            {},
            opts.map((m) => {
                const cls = current.has(`${m.peripheral}_${m.signal}`) ? 'current' : '';
                return h('tr', {}, h('td', { class: cls }, m.func), h('td', { class: cls }, m.peripheral), h('td', { class: cls }, m.signal));
            }),
        ),
    );
}

// ---- Drivers view ----------------------------------------------------------------

function driversView(): HTMLElement {
    const p = state.project!;
    const drivers = p.drivers().filter((d) => d.kind !== 'system');
    if (state.driver && !p.driver(state.driver)) state.driver = null;
    const errorsByDriver = new Set(issues().filter((i) => i.level === 'error' && i.driver).map((i) => i.driver));
    return h(
        'div',
        { class: 'split' },
        h(
            'section',
            { class: 'panel list' },
            h('div', { class: 'toolbar' }, h('h2', { style: 'margin:0;flex:1' }, 'Drivers'), h('button', { onclick: () => ((state.adding = true), (state.driver = null), render()) }, 'Add SPI or I2C master')),
            drivers.map((d) =>
                h(
                    'button',
                    {
                        class: `item ${state.driver === d.label && !state.adding ? 'selected' : ''}`,
                        onclick: () => ((state.driver = d.label), (state.adding = false), render()),
                    },
                    h('span', { class: 'mono' }, d.label),
                    errorsByDriver.has(d.label) && h('span', { class: 'badge err' }, '!'),
                    h('div', { class: 'sub' }, `${KIND_NAMES[d.kind]} · ${d.instance} · ${p.signals(d.label).length} pins`),
                ),
            ),
            h('p', { class: 'note' }, 'System blocks (clocks, DMAC, PORT, CMCC, RAMECC) keep their Atmel START settings. This tool does not edit them.'),
        ),
        h('aside', { class: 'panel sticky' }, state.adding ? addDriverForm() : state.driver ? driverPanel(p.driver(state.driver)!) : h('p', { class: 'note' }, 'Select a driver.')),
    );
}

function driverPanel(d: Driver): HTMLElement {
    const p = state.project!;
    const fields = settingFields(state.zip!, p, d);
    const signals = p.signals(d.label);
    const optional = d.kind === 'adc' || d.kind === 'pwm' || d.kind === 'dac';
    return h(
        'div',
        {},
        h('h2', {}, d.label),
        h(
            'div',
            { class: 'kv' },
            h('label', {}, 'Name'),
            textInput(d.label, (v) => {
                const before = state.undo.length;
                edit((pr) => pr.renameDriver(d.label, v));
                if (state.undo.length > before) (state.driver = v), render();
            }, { class: 'mono', 'aria-label': 'Driver name' }),
            h('div', { class: 'field-help' }, 'This is the C descriptor name. Rename the uses in the firmware too.'),
            h('label', {}, 'Type'),
            h('span', {}, KIND_NAMES[d.kind]),
            h('label', {}, 'Hardware'),
            h('span', { class: 'mono' }, d.instance),
            d.kind === 'spi' && h('label', {}, 'Pad layout'),
            d.kind === 'spi' && h('span', { class: 'mono' }, String((d.raw.variant as YamlMap).specification)),
        ),
        signals.length > 0 && h('h3', {}, 'Pins'),
        signals.length > 0 &&
            h(
                'table',
                {},
                h('thead', {}, h('tr', {}, h('th', {}, 'Signal'), h('th', {}, 'Pin'), h('th', {}, 'Label'))),
                h(
                    'tbody',
                    {},
                    signals.map((s) =>
                        h(
                            'tr',
                            {},
                            h('td', { class: 'mono' }, s.role),
                            h('td', {}, select(s.pin, moveTargets(d, s), (v) => edit((pr) => pr.moveSignal(d.label, s.name, v)), { class: 'mono', 'aria-label': `${s.role} pin` })),
                            h('td', { class: 'mono' }, h('a', { href: '#', onclick: (e: Event) => (e.preventDefault(), (state.tab = 'pins'), (state.pin = s.pin), render()) }, p.padAt(s.pin)?.label ?? '?')),
                        ),
                    ),
                ),
            ),
        optional && h('p', { class: 'note' }, 'To add a channel, select a free pin in the Pins tab and use "Give it to a driver".'),
        fields.length > 0 && h('h3', {}, 'Settings'),
        fields.length > 0 && h('div', { class: 'kv' }, fields.flatMap((f) => settingRow(d, f))),
        (d.kind === 'spi' || d.kind === 'i2c') &&
            h(
                'div',
                { class: 'row-actions' },
                h(
                    'button',
                    {
                        class: 'danger',
                        onclick: () => {
                            if (confirm(`Delete ${d.label} and free its pins? The firmware code that uses ${d.label} will stop compiling.`)) {
                                edit((pr) => pr.removeDriver(d.label));
                            }
                        },
                    },
                    `Delete ${d.label}`,
                ),
            ),
    );
}

function settingRow(d: Driver, f: ReturnType<typeof settingFields>[number]): HTMLElement[] {
    const set = (v: YamlValue) => edit((p) => p.setDriverSetting(d.label, f.key, v));
    let control: HTMLElement;
    const disabled = !f.editable;
    if (f.kind === 'select') {
        control = select(String(f.value), f.options.includes(String(f.value)) ? f.options : [String(f.value), ...f.options], (v) => set(v), { disabled });
    } else if (f.kind === 'bool') {
        control = h('input', { type: 'checkbox', checked: f.value === true, disabled, onchange: (e: Event) => set((e.target as HTMLInputElement).checked) });
    } else if (f.kind === 'number') {
        control = h('input', {
            type: 'number',
            value: String(f.value),
            min: f.range?.[0],
            max: f.range?.[1],
            disabled,
            onchange: (e: Event) => {
                const v = Number((e.target as HTMLInputElement).value);
                if (!Number.isInteger(v)) return toast(`${f.title}: enter a whole number`);
                if (f.range && (v < f.range[0] || v > f.range[1])) return toast(`${f.title}: the range is ${f.range[0]} to ${f.range[1]}`);
                set(v);
            },
        });
    } else {
        control = h('input', { type: 'text', value: String(f.value), disabled: true });
    }
    const help = [f.help, f.reason].filter(Boolean).join(' ');
    const row: HTMLElement[] = [h('label', { title: f.key }, f.title), control];
    if (help) row.push(h('div', { class: 'field-help' }, help));
    return row;
}

function addDriverForm(): HTMLElement {
    const p = state.project!;
    const usedSercoms = new Set(p.drivers().map((d) => d.instance));
    const freeSercoms = [0, 1, 2, 3, 4, 5, 6, 7].filter((n) => !usedSercoms.has(`SERCOM${n}`));
    if (freeSercoms.length === 0) {
        return h(
            'div',
            {},
            h('h2', {}, 'Add SPI or I2C master'),
            h('p', { class: 'note' }, 'All eight SERCOMs are in use. Delete an SPI or I2C driver first to free its SERCOM.'),
        );
    }
    const form = { kind: 'spi' as 'spi' | 'i2c', sercom: freeSercoms[0], label: '', pins: {} as Record<string, string> };
    const container = h('div', {});
    const draw = () => {
        const roles = form.kind === 'spi' ? ['MOSI', 'SCK', 'MISO'] : ['SDA', 'SCL'];
        const freePin = (pin: string) => !p.padAt(pin) && p.signalAt(pin).length === 0 && !RESERVED_PINS[pin];
        const roleRows = roles.flatMap((role) => {
            const choices = sercomPinChoices(form.kind, `SERCOM${form.sercom}`, role).filter((c) => freePin(c.pin));
            if (!choices.some((c) => c.pin === form.pins[role])) form.pins[role] = choices[0]?.pin ?? '';
            return [
                h('label', {}, role),
                choices.length
                    ? select(form.pins[role], choices.map((c) => [c.pin, `${c.pin} (PAD${c.pad})`] as [string, string]), (v) => (form.pins[role] = v), { class: 'mono' })
                    : h('span', { class: 'note' }, 'No free pin can carry this signal'),
            ];
        });
        container.replaceChildren(
            h('h2', {}, 'Add SPI or I2C master'),
            h(
                'div',
                { class: 'kv' },
                h('label', {}, 'Type'),
                select(form.kind, [['spi', 'SPI master (sync)'], ['i2c', 'I2C master (sync)']], (v) => ((form.kind = v as 'spi'), (form.pins = {}), draw())),
                h('label', {}, 'SERCOM'),
                select(String(form.sercom), freeSercoms.map((n) => [String(n), `SERCOM${n}`] as [string, string]), (v) => ((form.sercom = Number(v)), (form.pins = {}), draw())),
                h('label', {}, 'Name'),
                h('input', { type: 'text', class: 'mono', placeholder: form.kind === 'spi' ? 'SPI_NEW' : 'I2C_NEW', value: form.label, oninput: (e: Event) => (form.label = (e.target as HTMLInputElement).value.trim()) }),
                roleRows,
            ),
            h('p', { class: 'note' }, 'Pads get labels like NEW_MOSI. Rename them in the Pins tab. Settings start from the values of an existing driver of the same type.'),
            h(
                'div',
                { class: 'row-actions' },
                h(
                    'button',
                    {
                        class: 'primary',
                        onclick: () => {
                            const label = form.label || (form.kind === 'spi' ? 'SPI_NEW' : 'I2C_NEW');
                            const tpl = (form.kind === 'spi' ? templates.spiDriver : templates.i2cDriver) as YamlMap;
                            const before = state.undo.length;
                            edit((pr) => pr.addSercomDriver(form.kind, label, form.sercom, form.pins, tpl));
                            if (state.undo.length > before) {
                                state.adding = false;
                                state.driver = label;
                                render();
                            }
                        },
                    },
                    'Add driver',
                ),
                h('button', { onclick: () => ((state.adding = false), render()) }, 'Cancel'),
            ),
        );
    };
    draw();
    return container;
}

// ---- Changes view ----------------------------------------------------------------

function changesView(): HTMLElement {
    const result = tryGenerate();
    if ('error' in result) {
        return h('section', { class: 'panel' }, h('h2', {}, 'Cannot generate yet'), h('pre', { class: 'block' }, result.error));
    }
    const { out } = result;
    const items = [...out.changed, README_VIEW];
    if (!state.file || !items.includes(state.file)) state.file = out.changed[0] ?? README_VIEW;
    const p = state.project!;
    let view: HTMLElement;
    if (state.file === README_VIEW) {
        const md = markdownPinTables(p);
        view = h(
            'div',
            {},
            h(
                'div',
                { class: 'toolbar' },
                h('span', { class: 'note', style: 'flex:1' }, 'Paste this into the PVDX-SAMD-PinConfig README so that the README matches the .atzip.'),
                h('button', { onclick: () => navigator.clipboard.writeText(md).then(() => toast('Copied', 'info')) }, 'Copy'),
            ),
            h('pre', { class: 'block' }, md),
        );
    } else {
        const file = state.file;
        const old = state.zip!.files.has(file) ? readText(state.zip!, file) : '';
        const lines = diffLines(old, out.files.get(file)!);
        const hs = hunks(lines);
        view = h(
            'div',
            { class: 'diff' },
            hs.flatMap((hunk, i) => [
                h('div', { class: 'hunk-sep' }, i === 0 && hunk.lines[0]?.oldNo === 1 ? 'start of file' : `line ${hunk.lines[0]?.oldNo ?? hunk.lines[0]?.newNo}`),
                ...hunk.lines.map((l) =>
                    h(
                        'div',
                        { class: `line ${l.op === '+' ? 'add' : l.op === '-' ? 'del' : ''}` },
                        h('span', { class: 'no' }, `${l.op === '+' ? '' : l.oldNo ?? ''}`),
                        h('span', { class: 'no' }, `${l.op === '-' ? '' : l.newNo ?? ''}`),
                        `${l.op} ${l.text}`,
                    ),
                ),
            ]),
        );
    }
    return h(
        'div',
        { class: 'split', style: 'grid-template-columns: minmax(0, 280px) minmax(0, 1fr)' },
        h(
            'section',
            { class: 'panel list' },
            h('h2', {}, out.changed.length ? `${out.changed.length} changed files` : 'No changes'),
            items.map((f) => h('button', { class: `item mono ${state.file === f ? 'selected' : ''}`, onclick: () => ((state.file = f), render()) }, f)),
            h('p', { class: 'note' }, 'All other files of the .atzip are copied unchanged.'),
        ),
        h('section', { class: 'panel' }, h('h2', {}, state.file), view),
    );
}

// ---- Problems view ----------------------------------------------------------------

function problemsView(): HTMLElement {
    const all = issues();
    return h(
        'section',
        { class: 'panel', style: 'max-width: 900px' },
        h('h2', {}, all.length ? 'Problems' : 'No problems'),
        all.length === 0 && h('p', { class: 'note' }, 'The configuration is valid. You can download the .atzip.'),
        all.map((i) =>
            h(
                'div',
                {
                    class: `issue ${i.level}`,
                    onclick: () => {
                        if (i.pins[0]) Object.assign(state, { tab: 'pins', pin: i.pins[0] });
                        else if (i.driver) Object.assign(state, { tab: 'drivers', driver: i.driver });
                        render();
                    },
                },
                `${i.level === 'error' ? 'Error' : 'Warning'}: ${i.message}`,
            ),
        ),
    );
}

// ---- Start ---------------------------------------------------------------------

document.addEventListener('keydown', (e) => {
    if (!(e.ctrlKey || e.metaKey) || e.key.toLowerCase() !== 'z') return;
    if ((e.target as HTMLElement).matches('input, select, textarea')) return;
    e.preventDefault();
    if (e.shiftKey) redo();
    else undo();
});

window.addEventListener('beforeunload', (e) => {
    if (state.undo.length > 0) e.preventDefault();
});

render();
