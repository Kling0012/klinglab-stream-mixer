import { WebSocketServer, WebSocket } from 'ws';
import { performance } from 'node:perf_hooks';
import { pathToFileURL } from 'node:url';
import { VERSION, MAX_PAYLOAD, isObject, isId, isIndex } from '../site.klinglab.streammixer.sdPlugin/src/protocol.mjs';

const clone = value => structuredClone(value);
const ERRORS = {
    VERSION_MISMATCH: '通信バージョンが一致しません。', INVALID_MESSAGE: '要求の形式が正しくありません。',
    INVALID_ARGUMENT: '引数が正しくありません。', UNKNOWN_COMMAND: '対応していない操作です。',
    EMPTY_SLOT: 'この番号には登録がありません。', BUSY: '切り替え中です。少し待ってください。',
    LIMIT_REACHED: '同時に再生できる効果音は16個までです。', TOO_QUIET: '音が小さすぎるため学習できませんでした。',
    LOAD_FAILED: 'プロジェクトを読み込めませんでした。', INTERNAL_ERROR: '操作を処理できませんでした。',
};
const failure = code => ({ code, message: ERRORS[code] });

export function initialState() {
    return {
        mixerVersion: '0.1.0',
        fxChain: [{ n: 1, name: 'Voice EQ', bypass: false }, { n: 2, name: 'Voice Compressor', bypass: true }],
        streamFx: [{ n: 1, name: 'Bus EQ', bypass: false }],
        projects: {
            slots: Array.from({ length: 9 }, (_, i) => ({ n: i + 1, name: ['Talk', 'Song'][i] ?? null })),
            current: { n: 1, name: 'Talk' }, loading: null,
        },
        bgm: [{ n: 1, name: 'Opening', playing: false }, { n: 2, name: 'Break', playing: false }],
        se: [{ n: 1, name: 'Bell', playing: false }, { n: 2, name: 'Applause', playing: false }],
        loud: { on: false, mode: 'STREAM', learning: false, remainingSeconds: 0, learnedDb: 4.3 },
    };
}

export async function startMock({ port = 47321, version = VERSION, quiet = false, failProject = false } = {}) {
    const state = initialState();
    let revision = 0;
    let bgmTimer;
    let bgmEnd;
    let projectTimer;
    let learnTimer;
    let learnStart;
    const seTimers = new Map();
    const server = new WebSocketServer({ host: '127.0.0.1', port, maxPayload: MAX_PAYLOAD,
        verifyClient: info => !info.req.headers.origin });
    await new Promise((resolve, reject) => {
        server.once('listening', resolve);
        server.once('error', reject);
    });
    // A listener remains after startup so an isolated connection cannot crash the mock process.
    server.on('error', error => console.error(`mock-server: ${error.message}`));

    function send(socket, message) {
        if (socket.readyState !== WebSocket.OPEN) return;
        try { socket.send(JSON.stringify({ ...message, version }), () => {}); } catch { socket.terminate(); }
    }
    function broadcast(message) {
        for (const socket of server.clients) send(socket, message);
    }
    function update(changes) {
        Object.assign(state, clone(changes));
        broadcast({ type: 'event', revision: ++revision, changes });
    }
    function asyncError(code) { broadcast({ type: 'error', error: failure(code) }); }
    function result(socket, id, code) {
        send(socket, { type: 'result', id, ok: !code, ...(code ? { error: failure(code) } : {}) });
    }
    function soundChange(kind, n, playing) {
        update({ [kind]: state[kind].map(item => item.n === n ? { ...item, playing } : item) });
    }
    function stopBgm(next = null) {
        clearTimeout(bgmTimer);
        clearTimeout(bgmEnd);
        if (!state.bgm.some(item => item.playing)) {
            bgmTimer = null;
            if (next !== null) beginBgm(next);
            return;
        }
        bgmTimer = setTimeout(() => {
            bgmTimer = null;
            update({ bgm: state.bgm.map(item => ({ ...item, playing: false })) });
            if (next !== null) beginBgm(next);
        }, 1000);
    }
    function beginBgm(n) {
        update({ bgm: state.bgm.map(item => ({ ...item, playing: item.n === n })) });
        bgmEnd = setTimeout(() => soundChange('bgm', n, false), 8000);
    }
    function finishLearn() {
        clearInterval(learnTimer);
        learnTimer = null;
        const tooQuiet = quiet || performance.now() - learnStart < 3000;
        update({ loud: { ...state.loud, learning: false, remainingSeconds: 0,
            learnedDb: tooQuiet ? state.loud.learnedDb : 5.0 } });
        return tooQuiet;
    }
    function dispatch(message) {
        const { command, n, kind } = message;
        const numbered = { toggleFx: ['fxChain', 9], loadProject: ['projects', 9],
            toggleStreamFx: ['streamFx', Number.MAX_SAFE_INTEGER],
            playBgm: ['bgm', Number.MAX_SAFE_INTEGER], playSe: ['se', Number.MAX_SAFE_INTEGER] };
        let item;
        if (Object.hasOwn(numbered, command)) {
            const [key, max] = numbered[command];
            if (!isIndex(n) || n > max) return 'INVALID_ARGUMENT';
            item = (key === 'projects' ? state.projects.slots : state[key]).find(entry => entry.n === n && entry.name !== null);
            if (!item) return 'EMPTY_SLOT';
        }
        switch (command) {
            case 'toggleFx':
            case 'toggleStreamFx': {
                const key = command === 'toggleFx' ? 'fxChain' : 'streamFx';
                update({ [key]: state[key].map(entry => entry.n === n ? { ...entry, bypass: !entry.bypass } : entry) });
                break;
            }
            case 'loadProject':
                if (state.projects.loading !== null) return 'BUSY';
                update({ projects: { ...state.projects, loading: n } });
                projectTimer = setTimeout(() => {
                    if (failProject) {
                        update({ projects: { ...state.projects, loading: null } });
                        asyncError('LOAD_FAILED');
                    } else {
                        const preset = initialState();
                        clearInterval(learnTimer);
                        learnTimer = null;
                        update({ projects: { ...state.projects, current: { n, name: item.name }, loading: null },
                            fxChain: preset.fxChain, streamFx: preset.streamFx,
                            loud: { ...preset.loud, on: n === 2, learnedDb: n === 2 ? 5 : 4.3 } });
                    }
                }, 300);
                break;
            case 'playBgm':
                if (bgmTimer) return 'BUSY';
                if (item.playing) stopBgm();
                else if (state.bgm.some(entry => entry.playing)) stopBgm(n);
                else beginBgm(n);
                break;
            case 'playSe':
                if (!item.playing && state.se.filter(entry => entry.playing).length >= 16) return 'LIMIT_REACHED';
                clearTimeout(seTimers.get(n));
                seTimers.delete(n);
                soundChange('se', n, !item.playing);
                if (!item.playing) seTimers.set(n, setTimeout(() => {
                    seTimers.delete(n);
                    soundChange('se', n, false);
                }, 3000));
                break;
            case 'stopSounds':
                if (!['bgm', 'se', 'all'].includes(kind)) return 'INVALID_ARGUMENT';
                if (kind !== 'se') stopBgm();
                if (kind !== 'bgm') {
                    for (const timer of seTimers.values()) clearTimeout(timer);
                    seTimers.clear();
                    if (state.se.some(entry => entry.playing)) update({ se: state.se.map(entry => ({ ...entry, playing: false })) });
                }
                break;
            case 'loudToggle': update({ loud: { ...state.loud, on: !state.loud.on } }); break;
            case 'loudMode': update({ loud: { ...state.loud, mode: state.loud.mode === 'STREAM' ? 'CD' : 'STREAM' } }); break;
            case 'loudLearn':
                if (state.loud.learning) return finishLearn() ? 'TOO_QUIET' : undefined;
                learnStart = performance.now();
                update({ loud: { ...state.loud, learning: true, remainingSeconds: 20 } });
                learnTimer = setInterval(() => {
                    const remaining = Math.max(0, Math.ceil(20 - (performance.now() - learnStart) / 1000));
                    if (!remaining) {
                        if (finishLearn()) asyncError('TOO_QUIET');
                    } else if (remaining !== state.loud.remainingSeconds) {
                        update({ loud: { ...state.loud, remainingSeconds: remaining } });
                    }
                }, 1000);
                break;
            default: return 'UNKNOWN_COMMAND';
        }
    }
    server.on('connection', socket => {
        socket.alive = true;
        let rateStart = performance.now();
        let count = 0;
        socket.on('error', () => {});
        socket.on('pong', () => { socket.alive = true; });
        send(socket, { type: 'state', revision, state });
        socket.on('message', (data, binary) => {
            if (binary) return socket.close(1003, 'Text JSON required');
            const now = performance.now();
            if (now - rateStart >= 1000) { rateStart = now; count = 0; }
            if (++count > 30) return socket.close(1008, 'Request rate exceeded');
            let message;
            try { message = JSON.parse(data.toString()); } catch { return result(socket, null, 'INVALID_MESSAGE'); }
            const id = isObject(message) && isId(message.id) ? message.id : null;
            if (!isObject(message) || !Number.isInteger(message.version)) return result(socket, id, 'INVALID_MESSAGE');
            if (message.version !== version) {
                result(socket, id, 'VERSION_MISMATCH');
                return socket.close(1002, 'Version mismatch');
            }
            if (message.type !== 'request' || !id || typeof message.command !== 'string') {
                return result(socket, id, 'INVALID_MESSAGE');
            }
            try { result(socket, id, dispatch(message)); }
            catch { result(socket, id, 'INTERNAL_ERROR'); }
        });
    });
    const heartbeat = setInterval(() => {
        for (const socket of server.clients) {
            if (!socket.alive) { socket.terminate(); continue; }
            socket.alive = false;
            try { socket.ping(); } catch { socket.terminate(); }
        }
    }, 10000);
    return {
        port: server.address().port,
        get state() { return clone(state); },
        async close() {
            clearInterval(heartbeat);
            clearTimeout(bgmTimer);
            clearTimeout(bgmEnd);
            clearTimeout(projectTimer);
            clearInterval(learnTimer);
            for (const timer of seTimers.values()) clearTimeout(timer);
            for (const socket of server.clients) socket.terminate();
            await new Promise(resolve => server.close(resolve));
        },
    };
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const args = process.argv.slice(2);
    const value = (key, fallback) => Number(args.find(arg => arg.startsWith(`--${key}=`))?.split('=')[1] ?? fallback);
    const port = value('port', 47321);
    const version = value('version', VERSION);
    if (!Number.isInteger(port) || port < 0 || port > 65535 || !Number.isInteger(version) || version < 1) {
        console.error('mock-server: invalid port or version');
        process.exitCode = 1;
    } else {
        try {
            const mock = await startMock({ port, version, quiet: args.includes('--quiet'), failProject: args.includes('--fail-project') });
            console.log(`Mock mixer listening on ws://127.0.0.1:${mock.port}/ (protocol ${version})`);
            for (const signal of ['SIGINT', 'SIGTERM']) process.once(signal, () => { void mock.close(); });
        } catch (error) {
            console.error(`mock-server: ${error.message}`);
            process.exitCode = 1;
        }
    }
}
