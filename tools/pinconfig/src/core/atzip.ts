// Loads and writes .atzip files. An .atzip is a plain zip of the generated ASF tree.
import JSZip from 'jszip';

export interface Atzip {
    // Every file of the archive, by path, in archive order.
    files: Map<string, Uint8Array>;
    dates: Map<string, Date>;
}

export const ATSTART = 'atmel_start_config.atstart';

export async function readAtzip(data: ArrayBuffer | Uint8Array): Promise<Atzip> {
    const zip = await JSZip.loadAsync(data);
    const files = new Map<string, Uint8Array>();
    const dates = new Map<string, Date>();
    for (const entry of Object.values(zip.files)) {
        if (entry.dir) continue;
        files.set(entry.name, await entry.async('uint8array'));
        dates.set(entry.name, entry.date);
    }
    if (!files.has(ATSTART)) throw new Error(`This archive has no ${ATSTART}. It is not an Atmel START export.`);
    return { files, dates };
}

const decoder = new TextDecoder();
const encoder = new TextEncoder();

export function readText(zip: Atzip, path: string): string {
    const data = zip.files.get(path);
    if (!data) throw new Error(`${path} is missing from the .atzip`);
    return decoder.decode(data);
}

// Writes a new archive: every original file, with `changes` replacing or adding text files.
export async function writeAtzip(base: Atzip, changes: Map<string, string>): Promise<Uint8Array> {
    const zip = new JSZip();
    const now = new Date();
    for (const [path, data] of base.files) {
        const changed = changes.get(path);
        zip.file(path, changed !== undefined ? encoder.encode(changed) : data, {
            date: changed !== undefined ? now : base.dates.get(path),
            createFolders: false,
        });
    }
    for (const [path, text] of changes) {
        if (!base.files.has(path)) zip.file(path, encoder.encode(text), { date: now, createFolders: false });
    }
    return zip.generateAsync({ type: 'uint8array', compression: 'DEFLATE', compressionOptions: { level: 9 } });
}
