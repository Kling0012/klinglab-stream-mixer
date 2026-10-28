export const VERSION = 1;
export const URL = 'ws://127.0.0.1:47321/';
export const MAX_PAYLOAD = 1024 * 1024;
export const STATE_KEYS = ['mixerVersion', 'fxChain', 'streamFx', 'projects', 'bgm', 'se', 'loud'];
export const ACTION_TYPES = ['fx', 'project', 'streamfx', 'bgm', 'se', 'stop', 'loud', 'learn', 'mode'];
export const COMMANDS = {
    fx: 'toggleFx', project: 'loadProject', streamfx: 'toggleStreamFx',
    bgm: 'playBgm', se: 'playSe', stop: 'stopSounds',
    loud: 'loudToggle', learn: 'loudLearn', mode: 'loudMode',
};

export const isObject = value => value !== null && typeof value === 'object' && !Array.isArray(value);
export const isNumber = value => typeof value === 'number' && Number.isFinite(value);
export const isIndex = value => Number.isSafeInteger(value) && value > 0;
export const isRevision = value => Number.isSafeInteger(value) && value >= 0;
export const isId = value => typeof value === 'string' && /^[A-Za-z0-9_-]{1,64}$/.test(value);
const isName = value => typeof value === 'string' && value.trim().length > 0 && [...value].length <= 256;
const isBool = value => typeof value === 'boolean';

function list(value, item) {
    return Array.isArray(value) && value.every((entry, i) =>
        isObject(entry) && isIndex(entry.n) && (i === 0 || value[i - 1].n < entry.n) && item(entry));
}

export function validField(key, value) {
    switch (key) {
        case 'mixerVersion': return isName(value);
        case 'fxChain':
        case 'streamFx': return list(value, item => isName(item.name) && isBool(item.bypass));
        case 'bgm':
        case 'se': return list(value, item => isName(item.name) && isBool(item.playing));
        case 'projects': return isObject(value) &&
            list(value.slots, item => item.n <= 9 && (item.name === null || isName(item.name))) &&
            value.slots.length === 9 &&
            (value.current === null || (isObject(value.current) && isName(value.current.name) &&
                (value.current.n === null || (isIndex(value.current.n) && value.current.n <= 9)))) &&
            (value.loading === null || (isIndex(value.loading) && value.loading <= 9));
        case 'loud': return isObject(value) && isBool(value.on) &&
            ['STREAM', 'CD'].includes(value.mode) && isBool(value.learning) &&
            Number.isInteger(value.remainingSeconds) && value.remainingSeconds >= 0 && value.remainingSeconds <= 20 &&
            (value.learning ? value.remainingSeconds > 0 : value.remainingSeconds === 0) && isNumber(value.learnedDb);
        default: return false;
    }
}

export function validState(value) {
    return isObject(value) && STATE_KEYS.every(key => validField(key, value[key]));
}

export function validChanges(value) {
    if (!isObject(value)) return false;
    const keys = STATE_KEYS.filter(key => Object.hasOwn(value, key));
    return keys.length > 0 && keys.every(key => validField(key, value[key]));
}

export function knownFields(value) {
    return Object.fromEntries(STATE_KEYS.filter(key => Object.hasOwn(value, key)).map(key => [key, value[key]]));
}

export function validError(value) {
    return isObject(value) && typeof value.code === 'string' && value.code.length > 0 &&
        typeof value.message === 'string';
}

export function normalizeSettings(type, settings = {}) {
    if (!isObject(settings)) settings = {};
    if (['fx', 'project', 'streamfx', 'bgm', 'se'].includes(type)) {
        // Missing settings use slot 1; malformed saved settings remain unavailable.
        return { n: settings.n === undefined ? 1 : settings.n };
    }
    if (type === 'stop') return { kind: settings.kind === undefined ? 'all' : settings.kind };
    return {};
}

export function selectedItem(type, settings, state) {
    const n = settings.n;
    if (!isIndex(n) || (['fx', 'project'].includes(type) && n > 9)) return undefined;
    const items = { fx: state.fxChain, project: state.projects.slots, streamfx: state.streamFx,
        bgm: state.bgm, se: state.se }[type];
    return items?.find(item => item.n === n && item.name !== null);
}

export function requestFor(type, settings, state) {
    if (!state || !Object.hasOwn(COMMANDS, type)) return null;
    if (['fx', 'project', 'streamfx', 'bgm', 'se'].includes(type)) {
        return selectedItem(type, settings, state) ? { command: COMMANDS[type], n: settings.n } : null;
    }
    if (type === 'stop') return ['bgm', 'se', 'all'].includes(settings.kind)
        ? { command: COMMANDS[type], kind: settings.kind } : null;
    return { command: COMMANDS[type] };
}
