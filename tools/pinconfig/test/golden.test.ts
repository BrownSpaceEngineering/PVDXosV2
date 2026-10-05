import { describe, expect, it } from 'vitest';
import { readText } from '../src/core/atzip.ts';
import { generate, loadProject } from '../src/core/generate.ts';
import { firstDifference, load, REFERENCES } from './helpers.ts';

// Loading a real Atmel START export and generating without edits must give the
// same bytes for every generated file.
describe.each(Object.entries(REFERENCES))('round trip of %s', (_name, path) => {
    it('regenerates every file byte for byte', async () => {
        const zip = await load(path);
        const project = loadProject(zip);
        expect(project.validate().filter((i) => i.level === 'error')).toEqual([]);
        const out = generate(zip, project);
        for (const file of out.changed) {
            expect.soft(firstDifference(zip.files.has(file) ? readText(zip, file) : '', out.files.get(file)!), file).toBe('identical');
        }
        expect(out.changed).toEqual([]);
    });
});
