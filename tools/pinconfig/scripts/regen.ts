// Regenerates the configuration-dependent files of an .atzip from its .atstart file.
//
//   node scripts/regen.ts <in.atzip>               report files that would change
//   node scripts/regen.ts <in.atzip> <out.atzip>   also write the regenerated archive
//
// With no edits the output must equal the input. Use this as a check that the
// tool understands an archive before you edit it in the app.
import { readFileSync, writeFileSync } from 'node:fs';
import { readAtzip, writeAtzip } from '../src/core/atzip.ts';
import { generate, loadProject } from '../src/core/generate.ts';

const [, , inPath, outPath] = process.argv;
if (!inPath) {
    console.error('usage: regen.ts <in.atzip> [out.atzip]');
    process.exit(2);
}
const zip = await readAtzip(readFileSync(inPath));
const project = loadProject(zip);
for (const issue of project.validate()) console.log(`${issue.level}: ${issue.message}`);
const out = generate(zip, project);
console.log(out.changed.length === 0 ? 'All generated files match the archive.' : `Files that differ:\n  ${out.changed.join('\n  ')}`);
if (outPath) {
    writeFileSync(outPath, await writeAtzip(zip, out.files));
    console.log(`Wrote ${outPath}`);
}
process.exit(out.changed.length === 0 ? 0 : 1);
