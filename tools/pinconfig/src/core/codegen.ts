// Generates atmel_start_pins.h, driver_init.c/.h and examples/driver_examples.c/.h.
//
// The templates reproduce Atmel START's output byte for byte, including the
// clang-format pass that START ran after it filled its templates. The golden
// tests in test/ compare the output with real START exports.
import { findMux, pinNumber, pinPort } from './device.ts';
import type { Driver, Pad, Project, Signal } from './project.ts';

const BANNER = `/*
 * Code generated from Atmel Start.
 *
 * This file will be overwritten when reconfiguring your Atmel Start project.
 * Please copy examples or other code you want to keep to a separate file
 * to avoid losing it when reconfiguring.
 */
`;

// ---- Formatting -------------------------------------------------------------

// START ran clang-format with AlignConsecutiveDeclarations and MaxEmptyLinesToKeep: 1.
// These are the only two rules that change the template output.
export function clangFormat(text: string): string {
    const lines = text.replace(/\n{3,}/g, '\n\n').split('\n');
    const DECL = /^((?:extern )?struct \w+) +(\w+;)$/;
    for (let i = 0; i < lines.length; ) {
        if (!DECL.test(lines[i])) {
            i++;
            continue;
        }
        let j = i;
        while (j < lines.length && DECL.test(lines[j])) j++;
        const parts = lines.slice(i, j).map((l) => DECL.exec(l)!);
        const width = Math.max(...parts.map((p) => p[1].length));
        for (let k = i; k < j; k++) {
            const p = parts[k - i];
            lines[k] = p[1].padEnd(width) + ' ' + p[2];
        }
        i = j;
    }
    return lines.join('\n');
}

// ---- atmel_start_pins.h -----------------------------------------------------

export function genPinsH(project: Project): string {
    const pads = project.pads().sort((a, b) => cmpPin(a.pin, b.pin));
    let out = BANNER;
    out += '#ifndef ATMEL_START_PINS_H_INCLUDED\n#define ATMEL_START_PINS_H_INCLUDED\n\n#include <hal_gpio.h>\n\n';
    out += '// SAMD51 has 14 pin functions\n\n';
    'ABCDEFGHIJKLMN'.split('').forEach((f, i) => (out += `#define GPIO_PIN_FUNCTION_${f} ${i}\n`));
    out += '\n';
    for (const p of pads) out += `#define ${p.label} GPIO(GPIO_PORT${pinPort(p.pin)}, ${pinNumber(p.pin)})\n`;
    out += '\n#endif // ATMEL_START_PINS_H_INCLUDED\n';
    return out;
}

function cmpPin(a: string, b: string): number {
    return a[1] === b[1] ? pinNumber(a) - pinNumber(b) : a.charCodeAt(1) - b.charCodeAt(1);
}

// ---- Pin set-up snippets ----------------------------------------------------

const PULL_C: Record<string, string> = { Off: 'GPIO_PULL_OFF', 'Pull-up': 'GPIO_PULL_UP', 'Pull-down': 'GPIO_PULL_DOWN' };

function setLevel(label: string, level: string): string {
    const pad = '\t                   ';
    return (
        `\n\tgpio_set_pin_level(${label},\n` +
        `${pad}// <y> Initial level\n${pad}// <id> pad_initial_level\n${pad}// <false"> Low\n${pad}// <true"> High\n` +
        `${pad}${level === 'High' ? 'true' : 'false'});\n`
    );
}

function setPull(label: string, pull: string): string {
    const pad = '\t                       ';
    return (
        `\n\tgpio_set_pin_pull_mode(${label},\n` +
        `${pad}// <y> Pull configuration\n${pad}// <id> pad_pull_config\n${pad}// <GPIO_PULL_OFF"> Off\n` +
        `${pad}// <GPIO_PULL_UP"> Pull-up\n${pad}// <GPIO_PULL_DOWN"> Pull-down\n${pad}${PULL_C[pull]});\n`
    );
}

const dirOut = (label: string) => `\n\t// Set pin direction to output\n\tgpio_set_pin_direction(${label}, GPIO_DIRECTION_OUT);\n`;
const dirIn = (label: string) => `\n\t// Set pin direction to input\n\tgpio_set_pin_direction(${label}, GPIO_DIRECTION_IN);\n`;
const dirOff = (label: string) => `\n\t// Disable digital pin circuitry\n\tgpio_set_pin_direction(${label}, GPIO_DIRECTION_OFF);\n`;
const func = (label: string, fn: string) => `\n\tgpio_set_pin_function(${label}, ${fn});\n`;

function outputPin(p: Pad, fn: string): string {
    return setLevel(p.label, p.level) + dirOut(p.label) + func(p.label, fn);
}

function inputPin(p: Pad, fn: string): string {
    return dirIn(p.label) + setPull(p.label, p.pull) + func(p.label, fn);
}

// ---- Driver templates -------------------------------------------------------

interface Ctx {
    project: Project;
    d: Driver;
    n: string; // Instance number as text, empty for single-instance peripherals.
    signals: Signal[];
}

interface Template {
    descriptor?: string; // C type of the driver descriptor.
    topDecl?: boolean; // START declares these descriptors first in driver_init.c.
    noBlankBeforeExtern?: boolean;
    noBlankBeforeInclude?: boolean;
    cInclude?: string; // HPL header included at the top of driver_init.c.
    hIncludes: string[];
    prototypes: (l: string) => string[];
    body: (c: Ctx) => string;
    initCall: (l: string) => string;
    example?: (l: string) => string;
    examplePrototype: boolean;
}

function padOf(c: Ctx, s: Signal): Pad {
    const pad = c.project.padAt(s.pin);
    if (!pad) throw new Error(`${c.d.label}: no pad entry for ${s.pin}`);
    return pad;
}

function muxOf(s: Signal): string {
    const m = findMux(s.pin, s.peripheral, s.signal);
    if (!m) throw new Error(`${s.pin} cannot carry ${s.peripheral} ${s.signal}`);
    return m.macro;
}

const gclk = (id: string, src: string) => `\thri_gclk_write_PCHCTRL_reg(GCLK, ${id}, ${src} | (1 << GCLK_PCHCTRL_CHEN_Pos));\n`;

function sercomBus(n: number): string {
    return n <= 1 ? 'A' : n <= 3 ? 'B' : 'D';
}

function tccBus(n: number): string {
    return n <= 1 ? 'B' : n <= 3 ? 'C' : 'D';
}

function analogPortInit(c: Ctx): string {
    let body = '';
    for (const s of c.signals) {
        const p = padOf(c, s);
        body += dirOff(p.label) + func(p.label, muxOf(s));
    }
    return `void ${c.d.label}_PORT_init(void)\n{\n${body}}\n`;
}

function sercomClock(c: Ctx): string {
    const n = c.n;
    return (
        `void ${c.d.label}_CLOCK_init(void)\n{\n` +
        gclk(`SERCOM${n}_GCLK_ID_CORE`, `CONF_GCLK_SERCOM${n}_CORE_SRC`) +
        gclk(`SERCOM${n}_GCLK_ID_SLOW`, `CONF_GCLK_SERCOM${n}_SLOW_SRC`) +
        `\n\thri_mclk_set_APB${sercomBus(Number(n))}MASK_SERCOM${n}_bit(MCLK);\n}\n`
    );
}

const TEMPLATES: Record<Exclude<Driver['kind'], 'system'>, Template> = {
    adc: {
        descriptor: 'adc_sync_descriptor',
        cInclude: '<hpl_adc_base.h>',
        hIncludes: ['<hal_adc_sync.h>'],
        prototypes: (l) => [`${l}_PORT_init`, `${l}_CLOCK_init`, `${l}_init`],
        body: (c) =>
            analogPortInit(c) +
            `\nvoid ${c.d.label}_CLOCK_init(void)\n{\n\thri_mclk_set_APBDMASK_ADC${c.n}_bit(MCLK);\n` +
            gclk(`ADC${c.n}_GCLK_ID`, `CONF_GCLK_ADC${c.n}_SRC`) +
            `}\n\nvoid ${c.d.label}_init(void)\n{\n\t${c.d.label}_CLOCK_init();\n\t${c.d.label}_PORT_init();\n` +
            `\tadc_sync_init(&${c.d.label}, ADC${c.n}, (void *)NULL);\n}\n`,
        initCall: (l) => `${l}_init();`,
        example: (l) =>
            `/**\n * Example of using ${l} to generate waveform.\n */\nvoid ${l}_example(void)\n{\n\tuint8_t buffer[2];\n\n` +
            `\tadc_sync_enable_channel(&${l}, 0);\n\n\twhile (1) {\n\t\tadc_sync_read_channel(&${l}, 0, buffer, 2);\n\t}\n}\n`,
        examplePrototype: true,
    },
    dac: {
        descriptor: 'dac_sync_descriptor',
        hIncludes: ['<hal_dac_sync.h>'],
        prototypes: (l) => [`${l}_PORT_init`, `${l}_CLOCK_init`, `${l}_init`],
        body: (c) =>
            analogPortInit(c) +
            `\nvoid ${c.d.label}_CLOCK_init(void)\n{\n\n\thri_mclk_set_APBDMASK_DAC_bit(MCLK);\n` +
            gclk('DAC_GCLK_ID', 'CONF_GCLK_DAC_SRC') +
            `}\n\nvoid ${c.d.label}_init(void)\n{\n\t${c.d.label}_CLOCK_init();\n\tdac_sync_init(&${c.d.label}, DAC);\n` +
            `\t${c.d.label}_PORT_init();\n}\n`,
        initCall: (l) => `${l}_init();`,
        example: (l) =>
            `/**\n * Example of using ${l} to generate waveform.\n */\nvoid ${l}_example(void)\n{\n\tuint16_t i = 0;\n\n` +
            `\tdac_sync_enable_channel(&${l}, 0);\n\n\tfor (;;) {\n\t\tdac_sync_write(&${l}, 0, &i, 1);\n\t\ti = (i + 1) % 1024;\n\t}\n}\n`,
        examplePrototype: true,
    },
    timer: {
        descriptor: 'timer_descriptor',
        topDecl: true,
        noBlankBeforeExtern: true,
        cInclude: '<hpl_rtc_base.h>',
        hIncludes: ['<hal_timer.h>'],
        prototypes: () => [],
        body: (c) =>
            `/**\n * \\brief Timer initialization function\n *\n * Enables Timer peripheral, clocks and initializes Timer driver\n */\n` +
            `static void ${c.d.label}_init(void)\n{\n\thri_mclk_set_APBAMASK_RTC_bit(MCLK);\n` +
            `\ttimer_init(&${c.d.label}, RTC, _rtc_get_timer());\n}\n`,
        initCall: (l) => `${l}_init();`,
        example: (l) =>
            `static struct timer_task ${l}_task1, ${l}_task2;\n/**\n * Example of using ${l}.\n */\n` +
            `static void ${l}_task1_cb(const struct timer_task *const timer_task)\n{\n}\n\n` +
            `static void ${l}_task2_cb(const struct timer_task *const timer_task)\n{\n}\n\n` +
            `void ${l}_example(void)\n{\n` +
            `\t${l}_task1.interval = 100;\n\t${l}_task1.cb       = ${l}_task1_cb;\n\t${l}_task1.mode     = TIMER_TASK_REPEAT;\n` +
            `\t${l}_task2.interval = 200;\n\t${l}_task2.cb       = ${l}_task2_cb;\n\t${l}_task2.mode     = TIMER_TASK_REPEAT;\n\n` +
            `\ttimer_add_task(&${l}, &${l}_task1);\n\ttimer_add_task(&${l}, &${l}_task2);\n\ttimer_start(&${l});\n}\n`,
        examplePrototype: true,
    },
    spi: {
        descriptor: 'spi_m_sync_descriptor',
        topDecl: true,
        noBlankBeforeExtern: true,
        noBlankBeforeInclude: true,
        hIncludes: ['<hal_spi_m_sync.h>'],
        prototypes: (l) => [`${l}_PORT_init`, `${l}_CLOCK_init`, `${l}_init`],
        body: (c) => {
            let port = '';
            for (const s of c.signals) {
                const p = padOf(c, s);
                port += s.role === 'MISO' ? inputPin(p, muxOf(s)) : outputPin(p, muxOf(s));
            }
            return (
                `void ${c.d.label}_PORT_init(void)\n{\n${port}}\n\n` +
                sercomClock(c) +
                `\nvoid ${c.d.label}_init(void)\n{\n\t${c.d.label}_CLOCK_init();\n` +
                `\tspi_m_sync_init(&${c.d.label}, SERCOM${c.n});\n\t${c.d.label}_PORT_init();\n}\n`
            );
        },
        initCall: (l) => `${l}_init();`,
        example: (l) =>
            `/**\n * Example of using ${l} to write "Hello World" using the IO abstraction.\n */\n` +
            `static uint8_t example_${l}[12] = "Hello World!";\n\nvoid ${l}_example(void)\n{\n` +
            `\tstruct io_descriptor *io;\n\tspi_m_sync_get_io_descriptor(&${l}, &io);\n\n` +
            `\tspi_m_sync_enable(&${l});\n\tio_write(io, example_${l}, 12);\n}\n`,
        examplePrototype: false,
    },
    i2c: {
        descriptor: 'i2c_m_sync_desc',
        hIncludes: ['<hal_i2c_m_sync.h>'],
        prototypes: (l) => [`${l}_CLOCK_init`, `${l}_init`, `${l}_PORT_init`],
        body: (c) => {
            let port = '';
            for (const s of c.signals) {
                const p = padOf(c, s);
                port += setPull(p.label, p.pull) + func(p.label, muxOf(s));
            }
            return (
                `void ${c.d.label}_PORT_init(void)\n{\n${port}}\n\n` +
                sercomClock(c) +
                `\nvoid ${c.d.label}_init(void)\n{\n\t${c.d.label}_CLOCK_init();\n` +
                `\ti2c_m_sync_init(&${c.d.label}, SERCOM${c.n});\n\t${c.d.label}_PORT_init();\n}\n`
            );
        },
        initCall: (l) => `${l}_init();`,
        example: (l) =>
            `void ${l}_example(void)\n{\n\tstruct io_descriptor *${l}_io;\n\n` +
            `\ti2c_m_sync_get_io_descriptor(&${l}, &${l}_io);\n\ti2c_m_sync_enable(&${l});\n` +
            `\ti2c_m_sync_set_slaveaddr(&${l}, 0x12, I2C_M_SEVEN);\n\tio_write(${l}_io, (uint8_t *)"Hello World!", 12);\n}\n`,
        examplePrototype: true,
    },
    delay: {
        hIncludes: ['<hal_delay.h>'],
        prototypes: () => ['delay_driver_init'],
        body: () => `void delay_driver_init(void)\n{\n\tdelay_init(SysTick);\n}\n`,
        initCall: () => 'delay_driver_init();',
        example: () => `void delay_example(void)\n{\n\tdelay_ms(5000);\n}\n`,
        examplePrototype: true,
    },
    pwm: {
        descriptor: 'pwm_descriptor',
        hIncludes: ['<hal_pwm.h>', '<hpl_tcc.h>'],
        prototypes: (l) => [`${l}_PORT_init`, `${l}_CLOCK_init`, `${l}_init`],
        body: (c) => {
            let port = '';
            for (const s of c.signals) port += func(padOf(c, s).label, muxOf(s));
            return (
                `void ${c.d.label}_PORT_init(void)\n{\n${port}}\n\n` +
                `void ${c.d.label}_CLOCK_init(void)\n{\n\n\thri_mclk_set_APB${tccBus(Number(c.n))}MASK_TCC${c.n}_bit(MCLK);\n` +
                gclk(`TCC${c.n}_GCLK_ID`, `CONF_GCLK_TCC${c.n}_SRC`) +
                `}\n\nvoid ${c.d.label}_init(void)\n{\n\t${c.d.label}_CLOCK_init();\n\t${c.d.label}_PORT_init();\n` +
                `\tpwm_init(&${c.d.label}, TCC${c.n}, _tcc_get_pwm());\n}\n`
            );
        },
        initCall: (l) => `${l}_init();`,
        example: (l) =>
            `/**\n * Example of using ${l}.\n */\nvoid ${l}_example(void)\n{\n` +
            `\tpwm_set_parameters(&${l}, 10000, 5000);\n\tpwm_enable(&${l});\n}\n`,
        examplePrototype: true,
    },
    rand: {
        descriptor: 'rand_sync_desc',
        hIncludes: ['<hal_rand_sync.h>'],
        prototypes: (l) => [`${l}_CLOCK_init`, `${l}_init`],
        body: (c) =>
            `void ${c.d.label}_CLOCK_init(void)\n{\n\thri_mclk_set_APBCMASK_TRNG_bit(MCLK);\n}\n\n` +
            `void ${c.d.label}_init(void)\n{\n\t${c.d.label}_CLOCK_init();\n\trand_sync_init(&${c.d.label}, TRNG);\n}\n`,
        initCall: (l) => `${l}_init();`,
        example: (l) =>
            `/**\n * Example of using ${l} to generate waveform.\n */\nvoid ${l}_example(void)\n{\n` +
            `\tuint32_t random_n[4];\n\trand_sync_enable(&${l});\n` +
            `\trandom_n[0] = rand_sync_read32(&${l});\n\trandom_n[1] = rand_sync_read32(&${l});\n` +
            `\trand_sync_read_buf32(&${l}, &random_n[2], 2);\n` +
            `\tif (random_n[0] == random_n[1]) {\n\t\t/* halt */\n\t\twhile (1)\n\t\t\t;\n\t}\n` +
            `\tif (random_n[2] == random_n[3]) {\n\t\t/* halt */\n\t\twhile (1)\n\t\t\t;\n\t}\n}\n`,
        examplePrototype: true,
    },
    wdt: {
        descriptor: 'wdt_descriptor',
        hIncludes: ['<hal_wdt.h>'],
        prototypes: (l) => [`${l}_CLOCK_init`, `${l}_init`],
        body: (c) =>
            `void ${c.d.label}_CLOCK_init(void)\n{\n\thri_mclk_set_APBAMASK_WDT_bit(MCLK);\n}\n\n` +
            `void ${c.d.label}_init(void)\n{\n\t${c.d.label}_CLOCK_init();\n\twdt_init(&${c.d.label}, WDT);\n}\n`,
        initCall: (l) => `${l}_init();`,
        example: (l) =>
            `/**\n * Example of using ${l}.\n */\nvoid ${l}_example(void)\n{\n\tuint32_t clk_rate;\n\tuint16_t timeout_period;\n\n` +
            `\tclk_rate       = 1000;\n\ttimeout_period = 4096;\n\twdt_set_timeout_period(&${l}, clk_rate, timeout_period);\n` +
            `\twdt_enable(&${l});\n}\n`,
        examplePrototype: true,
    },
};

function codeDrivers(project: Project): { d: Driver; t: Template }[] {
    return project
        .drivers()
        .filter((d) => d.kind !== 'system')
        .map((d) => ({ d, t: TEMPLATES[d.kind as Exclude<Driver['kind'], 'system'>] }));
}

// ---- driver_init.h ----------------------------------------------------------

export function genDriverInitH(project: Project): string {
    const drivers = codeDrivers(project);
    let out = BANNER;
    out += '#ifndef DRIVER_INIT_INCLUDED\n#define DRIVER_INIT_INCLUDED\n\n#include "atmel_start_pins.h"\n\n';
    out += '#ifdef __cplusplus\nextern "C" {\n#endif\n\n';
    out += ['hal_atomic', 'hal_delay', 'hal_gpio', 'hal_init', 'hal_io', 'hal_sleep'].map((h) => `#include <${h}.h>\n`).join('');
    for (const { t } of drivers) {
        if (!t.noBlankBeforeInclude) out += '\n';
        out += t.hIncludes.map((h) => `#include ${h}\n`).join('');
    }
    out += '\n';
    for (const { d, t } of drivers) {
        if (!t.descriptor) continue;
        if (!t.noBlankBeforeExtern) out += '\n';
        out += `extern struct ${t.descriptor} ${d.label};\n`;
    }
    for (const { d, t } of drivers) {
        const protos = t.prototypes(d.label);
        if (protos.length === 0) continue;
        out += '\n' + protos.map((p) => `void ${p}(void);\n`).join('');
    }
    out += '\n/**\n * \\brief Perform system initialization, initialize pins and clocks for\n * peripherals\n */\nvoid system_init(void);\n\n';
    out += '#ifdef __cplusplus\n}\n#endif\n#endif // DRIVER_INIT_INCLUDED\n';
    return clangFormat(out);
}

// ---- driver_init.c ----------------------------------------------------------

export function genDriverInitC(project: Project): string {
    const drivers = codeDrivers(project);
    let out = BANNER + '\n#include "driver_init.h"\n#include <peripheral_clk_config.h>\n#include <utils.h>\n#include <hal_init.h>\n\n';
    for (const { t } of drivers) if (t.cInclude) out += `#include ${t.cInclude}\n`;
    out += '\n';
    for (const { d, t } of drivers) if (t.topDecl) out += `struct ${t.descriptor} ${d.label};\n`;
    for (const { d, t } of drivers) if (t.descriptor && !t.topDecl) out += `\nstruct ${t.descriptor} ${d.label};\n`;
    for (const { d, t } of drivers) {
        const ctx: Ctx = { project, d, n: d.index === null ? '' : String(d.index), signals: project.signals(d.label) };
        out += '\n' + t.body(ctx);
    }
    out += '\nvoid system_init(void)\n{\n\tinit_mcu();\n';
    for (const p of project.gpioPads()) {
        out += `\n\t// GPIO on ${p.pin}\n`;
        out += p.mode === 'Digital input' ? inputPin(p, 'GPIO_PIN_FUNCTION_OFF') : outputPin(p, 'GPIO_PIN_FUNCTION_OFF');
    }
    for (const { d, t } of drivers) out += `\n\t${t.initCall(d.label)}\n`;
    out += '}\n';
    return clangFormat(out);
}

// ---- examples/driver_examples.c/.h ------------------------------------------

export function genExamplesC(project: Project): string {
    let out = BANNER + '\n#include "driver_examples.h"\n#include "driver_init.h"\n#include "utils.h"\n';
    for (const { d, t } of codeDrivers(project)) if (t.example) out += '\n' + t.example(d.label);
    return clangFormat(out);
}

export function genExamplesH(project: Project): string {
    let out = BANNER + '#ifndef DRIVER_EXAMPLES_H_INCLUDED\n#define DRIVER_EXAMPLES_H_INCLUDED\n\n#ifdef __cplusplus\nextern "C" {\n#endif\n';
    for (const { d, t } of codeDrivers(project)) {
        if (!t.examplePrototype) continue;
        out += `\nvoid ${d.kind === 'delay' ? 'delay' : d.label}_example(void);\n`;
    }
    out += '\n#ifdef __cplusplus\n}\n#endif\n#endif // DRIVER_EXAMPLES_H_INCLUDED\n';
    return clangFormat(out);
}
