// Pin database for the SAMD51P20A, built by scripts/gen-pinmux.ts from the device header.
import db from '../data/samd51p20a.json' with { type: 'json' };

export interface MuxEntry {
    func: string; // Peripheral function letter, A to N.
    peripheral: string; // For example SERCOM0, ADC1, TCC0, DAC.
    signal: string; // For example PAD2, AIN7, WO1, VOUT0.
    macro: string; // For example PINMUX_PC18D_SERCOM0_PAD2.
}

export const DEVICE: string = db.device;
export const PINS: readonly string[] = db.pins;
const FUNCTIONS = db.functions as Record<string, MuxEntry[]>;

// The SWD pins are reserved for the debugger.
export const RESERVED_PINS: Readonly<Record<string, string>> = { PA30: 'SWCLK', PA31: 'SWDIO' };

export function isPin(name: string): boolean {
    return name in FUNCTIONS;
}

export function pinPort(pin: string): string {
    return pin[1];
}

export function pinNumber(pin: string): number {
    return parseInt(pin.slice(2), 10);
}

// Sort key that matches the order Atmel START used: port, then pin number.
export function pinOrder(pin: string): number {
    return (pin.charCodeAt(1) - 65) * 32 + pinNumber(pin);
}

export function comparePins(a: string, b: string): number {
    return pinOrder(a) - pinOrder(b);
}

export function muxOptions(pin: string): readonly MuxEntry[] {
    return FUNCTIONS[pin] ?? [];
}

export function findMux(pin: string, peripheral: string, signal: string): MuxEntry | undefined {
    return muxOptions(pin).find((m) => m.peripheral === peripheral && m.signal === signal);
}

// All pins that can carry a given peripheral signal.
export function pinsFor(peripheral: string, signal: string): string[] {
    return PINS.filter((pin) => findMux(pin, peripheral, signal) !== undefined);
}
