import { beforeEach, describe, expect, it } from 'vitest';
import { readText, type Atzip } from '../src/core/atzip.ts';
import { generate, loadProject, templates } from '../src/core/generate.ts';
import type { Project } from '../src/core/project.ts';
import type { YamlMap } from '../src/core/yaml.ts';
import { diffLines, load, REFERENCES } from './helpers.ts';

let zip: Atzip;
let project: Project;

beforeEach(async () => {
    zip ??= await load(REFERENCES.final);
    project = loadProject(zip);
});

describe('GPIO edits', () => {
    it('moves an LED to a free pin', () => {
        project.removePad('PA19');
        project.setGpio('PA24', 'LED_RED', 'Digital output', 'High', 'Off');
        const out = generate(zip, project);
        expect(out.changed.sort()).toEqual(['atmel_start_config.atstart', 'atmel_start_pins.h', 'driver_init.c']);
        const pins = out.files.get('atmel_start_pins.h')!;
        expect(pins).toContain('#define LED_RED GPIO(GPIO_PORTA, 24)');
        expect(pins).not.toContain('GPIO(GPIO_PORTA, 19)');
        expect(out.files.get('driver_init.c')).toContain('\t// GPIO on PA24\n\n\tgpio_set_pin_level(LED_RED,');
    });

    it('adds an input with a pull-up', () => {
        project.setGpio('PC28', 'SPARE_IN', 'Digital input', 'Low', 'Pull-up');
        const c = generate(zip, project).files.get('driver_init.c')!;
        expect(c).toMatch(/\/\/ GPIO on PC28\n\n\t\/\/ Set pin direction to input\n\tgpio_set_pin_direction\(SPARE_IN, GPIO_DIRECTION_IN\);/);
        expect(c).toMatch(/GPIO_PULL_UP\);\n\n\tgpio_set_pin_function\(SPARE_IN, GPIO_PIN_FUNCTION_OFF\);/);
    });

    it('renames a pad', () => {
        project.renamePad('LED_RED', 'STATUS_LED');
        const out = generate(zip, project);
        expect(out.files.get('atmel_start_pins.h')).toContain('#define STATUS_LED GPIO(GPIO_PORTA, 19)');
        expect(out.files.get('driver_init.c')).not.toContain('LED_RED');
    });
});

describe('driver settings', () => {
    it('changes the SPI_MRAM baud rate', () => {
        project.setDriverSetting('SPI_MRAM', 'spi_master_baud_rate', 4000000);
        const out = generate(zip, project);
        expect(out.changed.sort()).toEqual(['atmel_start_config.atstart', 'config/hpl_sercom_config.h']);
        expect(diffLines(zip, 'config/hpl_sercom_config.h', out.files.get('config/hpl_sercom_config.h')!)).toEqual([
            '#define CONF_SERCOM_0_SPI_BAUD 50000 => #define CONF_SERCOM_0_SPI_BAUD 4000000',
        ]);
    });

    it('changes an option setting by its label', () => {
        project.setDriverSetting('SPI_DISPLAY', 'spi_master_arch_cpol', 'SCK is high when idle');
        project.setDriverSetting('I2C_SBAND', 'i2c_master_baud_rate', 400000);
        const out = generate(zip, project);
        expect(diffLines(zip, 'config/hpl_sercom_config.h', out.files.get('config/hpl_sercom_config.h')!)).toEqual([
            '#define CONF_SERCOM_1_SPI_CPOL 0x0 => #define CONF_SERCOM_1_SPI_CPOL 0x1',
            '#define CONF_SERCOM_3_I2CM_BAUD 100000 => #define CONF_SERCOM_3_I2CM_BAUD 400000',
        ]);
    });

    it('rejects a value that is not one of the options', () => {
        project.setDriverSetting('SPI_DISPLAY', 'spi_master_arch_cpol', 'sideways');
        expect(() => generate(zip, project)).toThrow(/not valid/);
    });
});

describe('signal moves', () => {
    it('moves SPI_SBAND MISO to PAD3 and updates RXPO', () => {
        project.moveSignal('SPI_SBAND', 'SERCOM5/PAD/2', 'PA25');
        const d = project.driver('SPI_SBAND')!;
        expect((d.raw.variant as YamlMap).specification).toBe('TXPO=0, RXPO=3');
        const out = generate(zip, project);
        expect(diffLines(zip, 'config/hpl_sercom_config.h', out.files.get('config/hpl_sercom_config.h')!)).toEqual([
            '#define CONF_SERCOM_5_SPI_RXPO 2 => #define CONF_SERCOM_5_SPI_RXPO 3',
        ]);
        expect(out.files.get('driver_init.c')).toContain('gpio_set_pin_function(SBAND_MISO, PINMUX_PA25D_SERCOM5_PAD3);');
        expect(out.files.get('atmel_start_pins.h')).toContain('#define SBAND_MISO GPIO(GPIO_PORTA, 25)');
    });

    it('moves SPI_SBAND MOSI to PAD3 and updates TXPO', () => {
        project.moveSignal('SPI_SBAND', 'SERCOM5/PAD/0', 'PA25');
        expect((project.driver('SPI_SBAND')!.raw.variant as YamlMap).specification).toBe('TXPO=2, RXPO=2');
        const sercom = generate(zip, project).files.get('config/hpl_sercom_config.h')!;
        expect(sercom).toContain('#define CONF_SERCOM_5_SPI_TXPO 2\n');
    });

    it('refuses a pin that cannot carry the signal', () => {
        expect(() => project.moveSignal('SPI_SBAND', 'SERCOM5/PAD/2', 'PA24')).not.toThrow();
        expect(() => project.moveSignal('SPI_MRAM', 'SERCOM0/PAD/1', 'PA24')).toThrow(/cannot carry/);
    });

    it('moves an ADC input and keeps its label', () => {
        project.removeOptionalSignal('ADC_1', 'ADC1/AIN/11');
        project.moveSignal('ADC_1', 'ADC1/AIN/10', 'PC01');
        expect(project.signals('ADC_1').map((s) => s.name)).toContain('ADC1/AIN/11');
        const out = generate(zip, project);
        expect(out.files.get('driver_init.c')).toContain('gpio_set_pin_function(PHD_0, PINMUX_PC01B_ADC1_AIN11);');
    });
});

describe('conflicts', () => {
    it('blocks generation when two users share a pin', () => {
        const pad = project.padAt('PA19')!;
        pad.raw.name = 'PA15';
        const issues = project.validate().filter((i) => i.level === 'error');
        expect(issues.some((i) => i.pins.includes('PA15'))).toBe(true);
        expect(() => generate(zip, project)).toThrow(/Fix these problems first/);
    });
});

describe('adding and deleting SERCOM drivers', () => {
    const UHF = { MOSI: 'PC12', SCK: 'PC13', MISO: 'PD10' };
    const UHF_LABELS = { MOSI: 'UHF_MOSI', SCK: 'UHF_SCK', MISO: 'UHF_MISO' };

    it('deletes SPI_UHF and re-adds it to give the same code', () => {
        const before = project.driver('SPI_UHF')!.raw.configuration;
        project.removeDriver('SPI_UHF');
        project.addSercomDriver('spi', 'SPI_UHF', 7, UHF, templates.spiDriver as YamlMap, UHF_LABELS);
        project.driver('SPI_UHF')!.raw.configuration = JSON.parse(JSON.stringify(before));
        const out = generate(zip, project);
        // Only the pad order inside the .atstart differs.
        expect(out.changed).toEqual(['atmel_start_config.atstart']);
    });

    it('turns SERCOM7 into an I2C master', () => {
        project.removeDriver('SPI_UHF');
        project.addSercomDriver('i2c', 'I2C_UHF', 7, { SDA: 'PC12', SCL: 'PC13' }, templates.i2cDriver as YamlMap);
        const out = generate(zip, project);
        const sercom = out.files.get('config/hpl_sercom_config.h')!;
        expect(sercom).toContain('#define CONF_SERCOM_7_I2CM_ENABLE 1');
        expect(sercom).not.toContain('CONF_SERCOM_7_SPI');
        const c = out.files.get('driver_init.c')!;
        expect(c).toContain('i2c_m_sync_init(&I2C_UHF, SERCOM7);');
        expect(c).toContain('gpio_set_pin_function(UHF_SDA, PINMUX_PC12C_SERCOM7_PAD0);');
        expect(c).toContain('hri_mclk_set_APBDMASK_SERCOM7_bit(MCLK);');
        expect(out.files.get('examples/driver_examples.h')).toContain('void I2C_UHF_example(void);');
        expect(out.files.get('config/peripheral_clk_config.h')).toBe(readText(zip, 'config/peripheral_clk_config.h'));
    });

    it('deletes a driver and its clock block', () => {
        project.removeDriver('SPI_UHF');
        const out = generate(zip, project);
        expect(out.files.get('config/hpl_sercom_config.h')).not.toContain('SERCOM_7');
        expect(out.files.get('config/peripheral_clk_config.h')).not.toContain('SERCOM7');
        expect(out.files.get('driver_init.c')).not.toContain('SPI_UHF');
        expect(out.files.get('atmel_start_pins.h')).not.toContain('UHF_MOSI');
    });

    it('refuses a SERCOM that is in use', () => {
        expect(() => project.addSercomDriver('spi', 'SPI_X', 7, UHF, templates.spiDriver as YamlMap)).toThrow(/already used/);
    });
});
