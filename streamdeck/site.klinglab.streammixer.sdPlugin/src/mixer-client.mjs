import { EventEmitter } from 'node:events';
import WebSocket from 'ws';
import { VERSION, URL, MAX_PAYLOAD, isObject, isId, isRevision, validState, validChanges,
    knownFields, validError } from './protocol.mjs';

export class MixerClient extends EventEmitter {
    constructor({ url = URL } = {}) {
        super();
        this.url = url;
        this.status = 'offline';
        this.state = null;
        this.revision = null;
        this.pending = new Map();
        this.nextId = 0;
        this.running = false;
        this.mismatch = false;
    }

    start() {
        if (this.running) return;
        this.running = true;
        this.connect();
    }

    stop() {
        this.running = false;
        clearTimeout(this.retry);
        this.clearConnectionTimers();
        this.socket?.terminate();
        this.socket = null;
        this.dropPending();
        this.setStatus('offline');
    }

    setStatus(status) {
        this.status = status;
        if (status !== 'ready') {
            this.state = null;
            this.revision = null;
        }
        this.emit('change');
    }

    clearConnectionTimers() {
        clearTimeout(this.snapshotTimer);
        clearTimeout(this.idleTimer);
    }

    touch(socket) {
        clearTimeout(this.idleTimer);
        this.idleTimer = setTimeout(() => this.invalidate(socket), 35000);
    }

    connect() {
        if (!this.running) return;
        let socket;
        try {
            socket = new WebSocket(this.url, { maxPayload: MAX_PAYLOAD, handshakeTimeout: 5000 });
        } catch {
            this.retry = setTimeout(() => this.connect(), 2000);
            return;
        }
        this.socket = socket;
        socket.on('open', () => {
            if (this.socket !== socket) return;
            this.snapshotTimer = setTimeout(() => this.invalidate(socket), 5000);
            this.touch(socket);
        });
        socket.on('ping', () => {
            if (this.socket === socket) this.touch(socket);
        });
        socket.on('message', (data, binary) => {
            if (this.socket !== socket) return;
            this.touch(socket);
            if (binary) return this.invalidate(socket);
            let message;
            try { message = JSON.parse(data.toString()); } catch { return this.invalidate(socket); }
            this.receive(socket, message);
        });
        socket.on('error', () => {
            // ws also emits close; reconnect is scheduled only there.
            if (this.socket === socket) this.invalidate(socket);
        });
        socket.on('close', () => {
            if (this.socket !== socket) return;
            this.clearConnectionTimers();
            this.socket = null;
            this.dropPending();
            this.setStatus(this.mismatch ? 'incompatible' : 'offline');
            if (this.running) this.retry = setTimeout(() => this.connect(), 2000);
        });
    }

    invalidate(socket, mismatch = false) {
        if (this.socket !== socket) return;
        if (mismatch) this.mismatch = true;
        this.dropPending();
        this.setStatus(this.mismatch ? 'incompatible' : 'offline');
        socket.terminate();
    }

    receive(socket, message) {
        if (!isObject(message) || !Number.isInteger(message.version)) return this.invalidate(socket);
        if (message.version !== VERSION) return this.invalidate(socket, true);
        switch (message.type) {
            case 'state':
                if (this.state || !isRevision(message.revision) || !validState(message.state)) {
                    return this.invalidate(socket);
                }
                clearTimeout(this.snapshotTimer);
                this.state = knownFields(message.state);
                this.revision = message.revision;
                this.mismatch = false;
                this.setStatus('ready');
                return;
            case 'event':
                if (!this.state || !isRevision(message.revision) || message.revision !== this.revision + 1 ||
                    !validChanges(message.changes)) return this.invalidate(socket);
                this.state = { ...this.state, ...knownFields(message.changes) };
                this.revision = message.revision;
                this.emit('change');
                return;
            case 'result': {
                if (!(isId(message.id) || message.id === null) || typeof message.ok !== 'boolean' ||
                    (!message.ok && !validError(message.error))) return this.invalidate(socket);
                if (!message.ok && message.error.code === 'VERSION_MISMATCH') return this.invalidate(socket, true);
                if (!this.state) return this.invalidate(socket);
                const request = this.pending.get(message.id);
                if (request) {
                    clearTimeout(request.timer);
                    this.pending.delete(message.id);
                    if (!message.ok) request.onError?.(message.error);
                }
                return;
            }
            case 'error':
                if (!this.state || !validError(message.error)) return this.invalidate(socket);
                this.emit('remoteError', message.error);
                return;
            default: this.invalidate(socket);
        }
    }

    dropPending() {
        for (const request of this.pending.values()) clearTimeout(request.timer);
        this.pending.clear();
    }

    request(command, onError) {
        const socket = this.socket;
        if (this.status !== 'ready' || socket?.readyState !== WebSocket.OPEN ||
            socket.bufferedAmount !== 0 || this.pending.size >= 32) return false;
        const id = String(++this.nextId);
        const timer = setTimeout(() => {
            this.pending.delete(id);
            onError?.({ code: 'TIMEOUT', message: 'ミキサーから応答がありません。' });
            this.invalidate(socket);
        }, 5000);
        this.pending.set(id, { timer, onError });
        try {
            socket.send(JSON.stringify({ ...command, type: 'request', version: VERSION, id }), error => {
                if (!error || this.socket !== socket) return;
                const request = this.pending.get(id);
                if (request) {
                    clearTimeout(request.timer);
                    this.pending.delete(id);
                    request.onError?.({ code: 'SEND_FAILED', message: '操作を送れませんでした。' });
                }
                this.invalidate(socket);
            });
            return true;
        } catch {
            clearTimeout(timer);
            this.pending.delete(id);
            this.invalidate(socket);
            return false;
        }
    }
}
