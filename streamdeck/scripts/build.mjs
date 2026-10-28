import { build } from 'esbuild';
import { Resvg } from '@resvg/resvg-js';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { ACTION_TYPES } from '../site.klinglab.streammixer.sdPlugin/src/protocol.mjs';
import { COLORS, renderButton } from '../site.klinglab.streammixer.sdPlugin/src/render.mjs';

const root = fileURLToPath(new URL('../', import.meta.url));
const plugin = path.join(root, 'site.klinglab.streammixer.sdPlugin');
const notices = ['Klinglab Stream Mixer Stream Deck plugin — AGPL-3.0-only',
    await readFile(path.join(root, '../LICENSE'), 'utf8')];
for (const name of ['@elgato/streamdeck', '@elgato/utils', 'ws', 'zod']) {
    const folder = path.join(root, 'node_modules', name);
    const metadata = JSON.parse(await readFile(path.join(folder, 'package.json'), 'utf8'));
    notices.push(`${name} ${metadata.version}\n${await readFile(path.join(folder, 'LICENSE'), 'utf8')}`);
}
await writeFile(path.join(plugin, 'LICENSES.txt'), notices.join('\n\n'));
await writeFile(path.join(plugin, 'README.txt'), 'Klinglab Stream Mixer Stream Deck plugin 0.1.0\nRequires macOS 14.4 and Stream Deck 7.1.\nRun Klinglab Stream Mixer, then assign actions and slot numbers in Stream Deck.\nMIXER OFF: mixer unavailable. UPDATE MIXER: protocol mismatch; update mixer and plugin together.\nSource code: streamdeck/ in the accompanying Klinglab Stream Mixer source distribution (AGPL-3.0-only).\n');
await mkdir(path.join(plugin, 'bin'), { recursive: true });
await mkdir(path.join(plugin, 'imgs'), { recursive: true });
await build({
    entryPoints: [path.join(plugin, 'src/plugin.mjs')],
    outfile: path.join(plugin, 'bin/plugin.js'), bundle: true, platform: 'node',
    format: 'cjs', target: 'node24', sourcemap: false, minify: false,
    external: ['bufferutil', 'utf-8-validate'], legalComments: 'eof',
});
async function png(name, svg, sizes) {
    for (const [width, suffix] of sizes) {
        const rendered = new Resvg(svg, { fitTo: { mode: 'width', value: width } });
        await writeFile(path.join(plugin, 'imgs', `${name}${suffix}.png`), rendered.render().asPng());
    }
}
const symbols = {
    fx: '<path d="M21 29h22M21 43h17M21 29v35M52 29l22 35M74 29L52 64"/>',
    project: '<path d="M22 24h51v50H22zM33 35h29M33 47h29M33 59h18"/>',
    streamfx: '<path d="M20 48h10l7-20 12 40 12-40 7 20h9"/>',
    bgm: '<path d="M37 62V28l31-6v34M37 34l31-6"/><ellipse cx="29" cy="64" rx="8" ry="6"/><ellipse cx="60" cy="58" rx="8" ry="6"/>',
    se: '<path d="M28 40h13l15-13v42L41 56H28zM65 36q15 12 0 24"/>',
    stop: '<rect x="29" y="29" width="38" height="38" rx="3"/>',
    loud: '<path d="M23 56h10V43H23zM43 56h10V32H43zM63 56h10V20H63zM20 69h57"/>',
    learn: '<circle cx="48" cy="48" r="25"/><circle cx="48" cy="48" r="10"/>',
    mode: '<path d="M26 32h43l-9-9M70 64H27l9 9M70 32l-10 9M26 64l10-9"/>',
};
for (const type of ACTION_TYPES) {
    await png(`${type}-key`, renderButton(type, {}, { status: 'offline', state: null }).svg, [[72, ''], [144, '@2x']]);
    const icon = `<svg xmlns="http://www.w3.org/2000/svg" width="96" height="96" viewBox="0 0 96 96"><g fill="none" stroke="#ffffff" stroke-width="5" stroke-linecap="round" stroke-linejoin="round">${symbols[type]}</g></svg>`;
    await png(type, icon, [[20, ''], [40, '@2x']]);
}
const brand = `<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256"><rect width="256" height="256" rx="40" fill="${COLORS.background}"/><g stroke="${COLORS.foreground}" stroke-width="12" stroke-linecap="round"><path d="M68 54v148M128 54v148M188 54v148"/></g><g fill="${COLORS.orange}"><rect x="44" y="86" width="48" height="24" rx="8"/><rect x="104" y="154" width="48" height="24" rx="8"/><rect x="164" y="112" width="48" height="24" rx="8"/></g></svg>`;
await png('plugin', brand, [[256, ''], [512, '@2x']]);
await png('category', `<svg xmlns="http://www.w3.org/2000/svg" width="96" height="96" viewBox="0 0 96 96"><g fill="none" stroke="#ffffff" stroke-width="5"><path d="M24 18v60M48 18v60M72 18v60M15 35h18M39 60h18M63 44h18"/></g></svg>`, [[28, ''], [56, '@2x']]);
console.log('Built plugin bundle and original PNG icons.');
