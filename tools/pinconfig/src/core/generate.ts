// Produces every file that depends on the project configuration.
import { ATSTART, readText, type Atzip } from './atzip.ts';
import { genDriverInitC, genDriverInitH, genExamplesC, genExamplesH, genPinsH } from './codegen.ts';
import {
    blockText,
    clockBlocks,
    endMarkLine,
    insertLines,
    patchSettings,
    readMacro,
    removeLines,
    sercomBlocks,
    setMacro,
} from './configh.ts';
import { Project, type Driver } from './project.ts';
import templates from '../data/templates.json' with { type: 'json' };
import { emitAtstart, parseAtstart, type YamlMap, type YamlValue } from './yaml.ts';

export interface Generated {
    files: Map<string, string>; // Every generated file, by path in the .atzip.
    changed: string[]; // Paths whose content differs from the base .atzip.
}

export function loadProject(zip: Atzip): Project {
    return new Project(parseAtstart(readText(zip, ATSTART)));
}

interface HeaderRule {
    file: string;
    owns: (d: Driver) => RegExp;
}

const HEADERS: Partial<Record<Driver['kind'], HeaderRule>> = {
    spi: { file: 'config/hpl_sercom_config.h', owns: (d) => new RegExp(`^CONF_SERCOM_${d.index}_`) },
    i2c: { file: 'config/hpl_sercom_config.h', owns: (d) => new RegExp(`^CONF_SERCOM_${d.index}_`) },
    adc: { file: 'config/hpl_adc_config.h', owns: (d) => new RegExp(`^CONF_ADC_${d.index}_`) },
    pwm: { file: 'config/hpl_tcc_config.h', owns: (d) => new RegExp(`^CONF_TCC${d.index}_`) },
    dac: { file: 'config/hpl_dac_config.h', owns: () => /^CONF_DAC/ },
    timer: { file: 'config/hpl_rtc_config.h', owns: () => /^CONF_(RTC|TAMPER)/ },
    rand: { file: 'config/hpl_trng_config.h', owns: () => /^CONF_TRNG/ },
    wdt: { file: 'config/hpl_wdt_config.h', owns: () => /^CONF_WDT/ },
    delay: { file: 'config/hpl_systick_config.h', owns: () => /^CONF_SYSTICK/ },
};

export function headerRule(d: Driver): HeaderRule | undefined {
    return HEADERS[d.kind];
}

function configOf(d: Driver | undefined): Record<string, YamlValue> {
    const c = d?.raw.configuration;
    return c && typeof c === 'object' && !Array.isArray(c) ? (c as YamlMap) : {};
}

// The driver in the base project that uses the same hardware instance and type.
export function baseCounterpart(project: Project, d: Driver): Driver | undefined {
    const base = new Project(project.base);
    return base.drivers().find((b) => b.instance === d.instance && b.kind === d.kind);
}

const fill = (text: string, n: number) => text.replace(/\{\{N\}\}/g, String(n));

function sercomType(text: string): 'spi' | 'i2c' {
    return /_I2CM_ENABLE/.test(text) ? 'i2c' : 'spi';
}

export function generate(base: Atzip, project: Project): Generated {
    const issues = project.validate().filter((i) => i.level === 'error');
    if (issues.length > 0) throw new Error(`Fix these problems first:\n${issues.map((i) => `- ${i.message}`).join('\n')}`);

    const files = new Map<string, string>();
    files.set(ATSTART, emitAtstart(project.doc));
    files.set('atmel_start_pins.h', genPinsH(project));
    files.set('driver_init.h', genDriverInitH(project));
    files.set('driver_init.c', genDriverInitC(project));
    files.set('examples/driver_examples.h', genExamplesH(project));
    files.set('examples/driver_examples.c', genExamplesC(project));

    const text = (path: string) => files.get(path) ?? readText(base, path);
    const sercomDrivers = project.drivers().filter((d) => d.kind === 'spi' || d.kind === 'i2c');

    // SERCOM blocks: drop blocks of removed drivers, add blocks for new ones.
    const sercomPath = 'config/hpl_sercom_config.h';
    const clockPath = 'config/peripheral_clk_config.h';
    if (base.files.has(sercomPath)) {
        let sercom = text(sercomPath);
        let clocks = text(clockPath);
        const wanted = new Map(sercomDrivers.map((d) => [`SERCOM${d.index}`, d]));
        for (const b of sercomBlocks(sercom).reverse()) {
            const d = wanted.get(b.key);
            if (!d || sercomType(blockText(sercom, b)) !== d.kind) sercom = removeLines(sercom, b);
        }
        for (const b of clockBlocks(clocks).reverse()) {
            if (b.key.startsWith('SERCOM') && !wanted.has(b.key)) clocks = removeLines(clocks, b);
        }
        const clockTemplate = (() => {
            const existing = clockBlocks(clocks).find((b) => b.key.startsWith('SERCOM'));
            if (!existing) return templates.sercomClockBlock;
            const n = existing.key.slice('SERCOM'.length);
            return blockText(clocks, existing)
                .replace(new RegExp(`SERCOM_${n}_`, 'g'), 'SERCOM_{{N}}_')
                .replace(new RegExp(`SERCOM${n}(?!\\d)`, 'g'), 'SERCOM{{N}}');
        })();
        for (const d of sercomDrivers) {
            const key = `SERCOM${d.index}`;
            const n = d.index as number;
            if (!sercomBlocks(sercom).some((b) => b.key === key)) {
                const after = sercomBlocks(sercom).find((b) => Number(b.key.slice(6)) > n);
                const tpl = d.kind === 'spi' ? templates.sercomSpiBlock : templates.sercomI2cBlock;
                sercom = insertLines(sercom, after ? after.start : endMarkLine(sercom), fill(tpl, n));
            }
            if (!clockBlocks(clocks).some((b) => b.key === key)) {
                const after = clockBlocks(clocks).find((b) => b.key > key);
                clocks = insertLines(clocks, after ? after.start : endMarkLine(clocks), fill(clockTemplate, n));
            }
        }
        files.set(sercomPath, sercom);
        files.set(clockPath, clocks);
    }

    // Driver settings.
    for (const d of project.drivers()) {
        const rule = HEADERS[d.kind];
        if (!rule || !base.files.has(rule.file)) continue;
        const counterpart = baseCounterpart(project, d);
        const before = counterpart
            ? configOf(counterpart)
            : ((d.kind === 'spi' ? templates.spiDriver.configuration : templates.i2cDriver.configuration) as Record<string, YamlValue>);
        let header = text(rule.file);
        header = patchSettings(header, rule.owns(d), before, configOf(d), d.label);
        if (d.kind === 'spi') {
            const spec = /TXPO=(\d+), RXPO=(\d+)/.exec(String((d.raw.variant as YamlMap).specification));
            if (spec) {
                for (const [name, value] of [['TXPO', spec[1]], ['RXPO', spec[2]]]) {
                    const macro = `CONF_SERCOM_${d.index}_SPI_${name}`;
                    const current = readMacro(header, macro);
                    if (current === undefined || Number(current) !== Number(value)) header = setMacro(header, macro, value);
                }
            }
        }
        files.set(rule.file, header);
    }

    const changed: string[] = [];
    for (const [path, content] of files) {
        if (!base.files.has(path) || readText(base, path) !== content) changed.push(path);
    }
    return { files, changed };
}

export { templates };
