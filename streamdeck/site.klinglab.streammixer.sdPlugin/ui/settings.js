/* Stream Deck calls this entry point when opening the property inspector. */
window.connectElgatoStreamDeckSocket = (port, uuid, registerEvent, info, actionInfo) => {
    const action = JSON.parse(actionInfo);
    const type = action.action.split('.').at(-1);
    const socket = new WebSocket(`ws://127.0.0.1:${port}`);
    let settings = action.payload.settings ?? {};
    const number = document.getElementById('number');
    const kind = document.getElementById('kind');
    const error = document.getElementById('error');
    const numbered = ['fx', 'project', 'streamfx', 'bgm', 'se'].includes(type);
    const max = ['fx', 'project'].includes(type) ? 9 : Number.MAX_SAFE_INTEGER;
    const names = { fx: 'FX On/Off', project: 'Project', streamfx: 'STREAM FX On/Off', bgm: 'BGM', se: 'SE',
        stop: 'Stop Sounds', loud: 'LOUD On/Off', learn: 'LOUD Learn', mode: 'LOUD Mode' };
    document.getElementById('heading').textContent = names[type] ?? 'Klinglab Stream Mixer';
    document.getElementById('number-field').hidden = !numbered;
    document.getElementById('kind-field').hidden = type !== 'stop';
    document.getElementById('fixed').hidden = numbered || type === 'stop';
    document.getElementById('hint').textContent = max === 9 ? '1〜9を指定してください。' : 'ミキサーに表示される番号を指定してください。';
    number.max = String(max);
    function display() {
        number.value = String(settings.n ?? 1);
        kind.value = settings.kind ?? 'all';
    }
    function save() {
        if (numbered) {
            const n = number.valueAsNumber;
            if (!Number.isSafeInteger(n) || n < 1 || n > max) {
                error.textContent = max === 9 ? '番号は1〜9の整数で指定してください。' : '番号は1以上の整数で指定してください。';
                error.hidden = false;
                return;
            }
            settings = { ...settings, n };
        } else if (type === 'stop') settings = { ...settings, kind: kind.value };
        error.hidden = true;
        if (socket.readyState === WebSocket.OPEN) {
            socket.send(JSON.stringify({ event: 'setSettings', context: uuid, payload: settings }));
        }
    }
    display();
    number.addEventListener('change', save);
    kind.addEventListener('change', save);
    document.getElementById('form').addEventListener('submit', event => { event.preventDefault(); save(); });
    socket.addEventListener('open', () => socket.send(JSON.stringify({ event: registerEvent, uuid })));
    socket.addEventListener('message', event => {
        let message;
        try { message = JSON.parse(event.data); } catch { return; }
        if (message.event === 'didReceiveSettings') {
            settings = message.payload.settings ?? {};
            display();
        }
    });
    socket.addEventListener('close', () => {
        error.textContent = '設定画面を閉じて、もう一度開いてください。';
        error.hidden = false;
    });
};
