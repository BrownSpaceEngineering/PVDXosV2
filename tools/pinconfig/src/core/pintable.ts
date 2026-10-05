// Markdown pin tables for the PVDX-SAMD-PinConfig README, generated from the project
// so that the README and the .atzip cannot disagree.
import { comparePins, PINS, RESERVED_PINS } from './device.ts';
import { KIND_NAMES, type Project } from './project.ts';

export function pinUse(project: Project, pin: string): string {
    const signals = project.signalAt(pin);
    if (signals.length > 0) return signals.map((s) => `${s.driver} ${s.role}`).join(', ');
    const pad = project.padAt(pin);
    if (!pad) return RESERVED_PINS[pin] ? `${RESERVED_PINS[pin]} (debug)` : '';
    if (pad.mode === 'Digital output') return `GPIO output, initial ${pad.level.toLowerCase()}`;
    if (pad.mode === 'Digital input') return pad.pull === 'Off' ? 'GPIO input' : `GPIO input, ${pad.pull.toLowerCase()}`;
    return pad.mode;
}

export function markdownPinTables(project: Project): string {
    let out = '## Drivers\n\n| Driver | Type | Hardware | Pins |\n|--------|------|----------|------|\n';
    for (const d of project.drivers()) {
        if (d.kind === 'system') continue;
        const pins = project
            .signals(d.label)
            .map((s) => `${s.role} ${s.pin} (${project.padAt(s.pin)?.label ?? '?'})`)
            .join(', ');
        out += `| ${d.label} | ${KIND_NAMES[d.kind]} | ${d.instance} | ${pins || '-'} |\n`;
    }
    out += '\n## Pins\n\n| Pin | Label | Use |\n|-----|-------|-----|\n';
    for (const pin of [...PINS].sort(comparePins)) {
        const pad = project.padAt(pin);
        const use = pinUse(project, pin);
        out += `| ${pin} | ${pad?.label ?? ''} | ${use || 'Unallocated'} |\n`;
    }
    return out;
}
