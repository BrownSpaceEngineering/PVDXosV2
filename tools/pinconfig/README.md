# PVDX Pin Configurator

This tool replaces Atmel START for the PVDX compute board (SAMD51P20A). Microchip took Atmel START offline, so the team now edits the pin and driver configuration here.

The tool is a web page that runs in your browser. It opens an Atmel START export (`.atzip`), lets you edit pins and drivers, and writes a new `.atzip`. The firmware build uses that file in the same way as an Atmel START export. Nothing leaves your computer.

## Start the tool

You need Node.js 22 or later and pnpm.

```bash
cd tools/pinconfig
pnpm install
pnpm dev
```

Open the address that Vite prints, usually `http://localhost:5173`.

To make a static copy of the page, run `pnpm build`. The page goes to `dist/`. You can open it from any web server or from GitHub Pages.

## Change the pin configuration

1. Open `PVDX-SAMD-PinConfig/<name>.atzip` in the tool.
2. Make your edits in the Pins tab and the Drivers tab.
3. Read the Problems tab. The download stays disabled while an error exists.
4. Read the Changes tab. It shows a diff of every generated file.
5. Click "Download .atzip".
6. Replace the old `.atzip` in `PVDX-SAMD-PinConfig` with the new file. Keep only one `.atzip` in that folder.
7. Copy the "README pin tables" text from the Changes tab into the PinConfig README.
8. From the PVDXosV2 root, run `make -C src update_asf`, then run `make dev`.
9. If the build passes, commit the submodule. Write which pins changed and why.

If you rename a pad or a driver, also rename its uses in the firmware. The pad label becomes the C macro in `atmel_start_pins.h`. The driver name becomes the C descriptor, for example `SPI_UHF`.

## What you can edit

- GPIO pins: create, delete, rename, direction, initial level, and pull.
- Driver pins: move SPI, I2C, ADC, DAC, and PWM signals to other legal pins. The tool shows only the pins that the multiplexer allows.
- SPI pad layout: the tool sets `TXPO` and `RXPO` from the pins that you choose.
- Driver configuration: every value that Atmel START stored in a `config/hpl_*_config.h` header, for example SPI baud rate, clock polarity, and I2C speed.
- SPI and I2C masters: delete one to free its SERCOM, or add one on a free SERCOM.
- ADC inputs, DAC outputs, and PWM outputs: enable or disable a channel on a pin.

## What you cannot edit

- Clocks, DMAC, CMCC, RAMECC, and the other system blocks keep their Atmel START values.
- Driver types that the `.atzip` does not contain, for example USART or EIC. The HAL source files for these types are not in the archive.
- New ADC, DAC, PWM, or timer instances. You can only edit the instances that exist.

## How the tool makes sure that the output is correct

An `.atzip` is a zip file of the complete `ASF/` tree. Only a few files depend on the configuration:

- `atmel_start_config.atstart`: the project file, in YAML.
- `atmel_start_pins.h`, `driver_init.c`, `driver_init.h`.
- `examples/driver_examples.c`, `examples/driver_examples.h`.
- The `config/hpl_*_config.h` headers and `config/peripheral_clk_config.h`.

The tool regenerates these files from the `.atstart` file. It copies all other files unchanged. The templates copy the Atmel START output byte for byte. The tests load two real Atmel START exports, regenerate them without edits, and compare every byte:

```bash
pnpm test
```

To make sure that the tool understands an archive before you edit it, run this command. It prints the files that the tool cannot reproduce exactly.

```bash
pnpm regen ../../PVDX-SAMD-PinConfig/FinalCompute.atzip
```

The app does the same check when it opens a file. If a file differs, the app shows a warning.

## Known issues in the current archives

- `FinalCompute.atzip` adds the TCC PWM drivers. Its `hpl_tcc.c` defines `TCC0_0_Handler` and `TCC1_0_Handler`. The firmware also defines them in `src/misc/exception_handlers/specific_handlers.c`, so the link fails. The cause is the pin configuration, not the tool. The committed `ASF/` still comes from the older `ASF.atzip` for this reason.
- Atmel START wrote `static uint8_t example_X[12] = "Hello World!";` for each SPI driver. GCC 15 rejects this with `-Werror`. The `update_asf` target in `src/Makefile` now changes the size to 13.

## Code layout

| Path | Contents |
|------|----------|
| `src/core/yaml.ts` | Reads and writes the `.atstart` file. The emitter copies PyYAML output, which Atmel START used. |
| `src/core/project.ts` | Project model, edit operations, and the checks in the Problems tab. |
| `src/core/codegen.ts` | Templates for the pins header, `driver_init`, and the driver examples. |
| `src/core/configh.ts` | Reads and patches the `config/` headers by their `// <id>` comments. |
| `src/core/generate.ts` | Puts the generated files together. |
| `src/core/device.ts` | Pin multiplexer database. |
| `src/data/samd51p20a.json` | Pin database. `pnpm gen-pinmux` makes it from the device header in `ASF/`. |
| `src/data/templates.json` | SPI and I2C blocks for new drivers. `pnpm gen-templates` makes it from `FinalCompute.atzip`. |
| `src/ui/` | The browser interface. |
| `scripts/regen.ts` | Command line regeneration check. |
| `test/` | Golden tests and edit tests. |

To support a new driver type, add a template to `TEMPLATES` in `src/core/codegen.ts` and a header rule to `HEADERS` in `src/core/generate.ts`. Then add a golden test with a real Atmel START export that contains that driver type.
