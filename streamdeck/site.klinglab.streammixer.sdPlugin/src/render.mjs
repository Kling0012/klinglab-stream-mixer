import { normalizeSettings, selectedItem } from './protocol.mjs';

export const COLORS = {
    background: '#191c22', foreground: '#f3f0e9', muted: '#6d737c',
    orange: '#f5a33a', green: '#74ce85', teal: '#53c5bd', pink: '#ec91bb',
};
const LABELS = { fx: 'FX CHAIN', project: 'PROJECT', streamfx: 'STREAM FX', bgm: 'BGM', se: 'SE',
    stop: 'STOP SOUNDS', loud: 'LOUD', learn: 'LOUD LEARN', mode: 'LOUD MODE' };
const xml = value => String(value).replace(/[\u0000-\u001f\u007f]/g, ' ').replace(/[&<>"']/g, char =>
    ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&apos;' })[char]);
function detailLines(value) {
    const lines = [''];
    let width = 0;
    const chars = [...value];
    for (let i = 0; i < chars.length; i++) {
        const char = chars[i];
        const size = char.codePointAt(0) > 255 ? 2 : 1;
        if (width + size > 19) {
            if (lines.length === 2) {
                lines[1] += '…';
                break;
            }
            lines.push('');
            width = 0;
        }
        lines[lines.length - 1] += char;
        width += size;
    }
    return lines;
}
const gain = db => `${db >= 0 ? '+' : ''}${db.toFixed(1)} dB`;

export function renderButton(type, rawSettings, { status, state }) {
    const settings = normalizeSettings(type, rawSettings);
    let title = '';
    let center = LABELS[type];
    let detail = '';
    let topColor = null;
    let lamp = null;
    let lampColor = COLORS.muted;
    let available = true;
    if (status !== 'ready' || !state) {
        title = status === 'incompatible' ? 'UPDATE MIXER' : 'MIXER OFF';
        center = status === 'incompatible' ? 'UPDATE' : 'MIXER';
        detail = status === 'incompatible' ? 'MIXER' : 'OFF';
        available = false;
    } else if (['fx', 'project', 'streamfx', 'bgm', 'se'].includes(type)) {
        const item = selectedItem(type, settings, state);
        if (!item) {
            title = '-';
            detail = '-';
            available = false;
        } else {
            title = `${item.n} ${item.name}`;
            detail = title;
            if (type === 'fx' || type === 'streamfx') {
                lamp = !item.bypass;
                lampColor = type === 'fx' ? COLORS.orange : COLORS.green;
            } else if (type === 'project') {
                if (state.projects.loading === item.n) topColor = COLORS.orange;
                center = state.projects.loading === item.n ? 'LOADING' :
                    state.projects.current?.n === item.n ? 'CURRENT' : 'PROJECT';
            } else if (item.playing) {
                topColor = type === 'bgm' ? COLORS.teal : COLORS.pink;
            }
        }
    } else if (type === 'stop') {
        available = ['bgm', 'se', 'all'].includes(settings.kind);
        title = available ? `STOP ${settings.kind === 'all' ? 'BGM + SE' : settings.kind.toUpperCase()}` : '-';
        center = 'STOP';
        detail = available ? title.slice(5) : '-';
        if (available && ((settings.kind !== 'se' && state.bgm.some(item => item.playing)) ||
            (settings.kind !== 'bgm' && state.se.some(item => item.playing)))) topColor = COLORS.orange;
    } else if (type === 'loud') {
        title = `LOUD ${state.loud.on ? 'ON' : 'OFF'}`;
        detail = state.loud.on ? 'ON' : 'OFF';
        if (state.loud.on) topColor = COLORS.orange;
    } else if (type === 'learn') {
        title = state.loud.learning ? `STOP ${state.loud.remainingSeconds}` : 'LOUD LEARN';
        center = state.loud.learning ? `STOP ${state.loud.remainingSeconds}` : 'LEARN';
        detail = gain(state.loud.learnedDb);
        if (state.loud.learning) topColor = COLORS.green;
    } else if (type === 'mode') {
        title = `${state.loud.mode}\n${gain(state.loud.learnedDb + (state.loud.mode === 'CD' ? 4.5 : 0))}`;
        center = state.loud.mode;
        detail = gain(state.loud.learnedDb + (state.loud.mode === 'CD' ? 4.5 : 0));
        if (state.loud.mode === 'CD') topColor = COLORS.orange;
    } else {
        available = false;
        title = '-';
        center = '-';
    }
    const lampSvg = lamp === null ? '' :
        `<circle cx="72" cy="38" r="11" fill="${lamp ? lampColor : COLORS.muted}"/><text x="72" y="67" font-size="14" fill="${COLORS.foreground}">${lamp ? 'ON' : 'OFF'}</text>`;
    const headerY = lamp === null ? 45 : 87;
    const lines = detailLines(detail);
    const detailY = lamp === null ? 88 : 112;
    const detailSvg = lines.map((line, i) => `<text x="72" y="${detailY + i * 17}" font-size="${lines.length > 1 || [...line].length > 12 ? 12 : 16}" fill="${COLORS.foreground}">${xml(line)}</text>`).join('');
    const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="144" height="144" viewBox="0 0 144 144"><rect width="144" height="144" rx="12" fill="${COLORS.background}"/>${topColor ? `<rect x="8" y="7" width="128" height="10" rx="4" fill="${topColor}"/>` : ''}<g font-family="Arial, Helvetica, sans-serif" text-anchor="middle" font-weight="bold">${lampSvg}<text x="72" y="${headerY}" font-size="15" fill="${COLORS.muted}">${xml(center)}</text>${detailSvg}</g></svg>`;
    return { title, svg, image: `data:image/svg+xml;base64,${Buffer.from(svg).toString('base64')}`, available };
}
