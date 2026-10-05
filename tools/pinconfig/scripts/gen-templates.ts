// Extracts the templates that the app needs to add a new SPI or I2C driver.
// The source is a real Atmel START export that contains at least one SPI master
// and one I2C master driver.
// Usage: node scripts/gen-templates.ts <reference.atzip> <out.json>
import { readFileSync, writeFileSync } from 'node:fs';
import { readAtzip, readText, ATSTART } from '../src/core/atzip.ts';
import { blockText, clockBlocks, sercomBlocks } from '../src/core/configh.ts';
import { parseAtstart, type YamlMap } from '../src/core/yaml.ts';

const [, , zipPath, outPath] = process.argv;
if (!zipPath || !outPath) {
    console.error('usage: gen-templates.ts <reference.atzip> <out.json>');
    process.exit(1);
}

const zip = await readAtzip(readFileSync(zipPath));
const doc = parseAtstart(readText(zip, ATSTART));
const drivers = Object.values(doc.drivers as YamlMap) as YamlMap[];
const instanceOf = (d: YamlMap) => String(d.definition).split('::')[2];
const spi = drivers.find((d) => d.api === 'HAL:Driver:SPI_Master_Sync');
const i2c = drivers.find((d) => d.api === 'HAL:Driver:I2C_Master_Sync');
if (!spi || !i2c) throw new Error('the reference needs one SPI master and one I2C master driver');

function generic(text: string, n: string): string {
    return text.replace(new RegExp(`SERCOM_${n}_`, 'g'), 'SERCOM_{{N}}_').replace(new RegExp(`SERCOM${n}(?!\\d)`, 'g'), 'SERCOM{{N}}');
}

const sercom = readText(zip, 'config/hpl_sercom_config.h');
const clocks = readText(zip, 'config/peripheral_clk_config.h');
function sercomBlock(d: YamlMap): string {
    const key = instanceOf(d);
    const b = sercomBlocks(sercom).find((x) => x.key === key)!;
    return generic(blockText(sercom, b), key.slice(6));
}
const spiKey = instanceOf(spi);
const clockBlock = clockBlocks(clocks).find((b) => b.key === spiKey)!;

function strip(d: YamlMap): YamlMap {
    const copy = JSON.parse(JSON.stringify(d)) as YamlMap;
    copy.user_label = 'NEW';
    (copy.variant as YamlMap).required_signals = [];
    return copy;
}

writeFileSync(
    outPath,
    JSON.stringify(
        {
            source: zipPath.split('/').pop(),
            spiDriver: strip(spi),
            i2cDriver: strip(i2c),
            sercomSpiBlock: sercomBlock(spi),
            sercomI2cBlock: sercomBlock(i2c),
            sercomClockBlock: generic(blockText(clocks, clockBlock), spiKey.slice(6)),
        },
        null,
        1,
    ) + '\n',
);
console.log(`templates from ${spi.user_label} (${spiKey}) and ${i2c.user_label} (${instanceOf(i2c)}) -> ${outPath}`);
