import { normalizeSettings, requestFor } from './protocol.mjs';
import { renderButton } from './render.mjs';

export class ButtonController {
    constructor(client, log = () => {}) {
        this.client = client;
        this.log = log;
        this.buttons = new Map();
        client.on('change', () => {
            for (const button of this.buttons.values()) this.refresh(button);
        });
        client.on('remoteError', error => {
            this.log(`${error.code}: ${error.message}`);
            const types = error.code === 'TOO_QUIET' ? ['learn'] :
                error.code === 'LOAD_FAILED' ? ['project'] : [];
            for (const button of this.buttons.values()) {
                if (types.includes(button.type)) this.alert(button.action);
            }
        });
    }

    appear(type, action, settings) {
        this.disappear(action.id);
        const button = { type, action, settings: normalizeSettings(type, settings),
            active: true, dirty: false, drawing: false, last: null };
        this.buttons.set(action.id, button);
        this.refresh(button);
    }

    disappear(id) {
        const button = this.buttons.get(id);
        if (button) button.active = false;
        this.buttons.delete(id);
    }

    settings(type, action, settings) {
        const button = this.buttons.get(action.id);
        if (!button) return this.appear(type, action, settings);
        button.settings = normalizeSettings(type, settings);
        this.refresh(button);
    }

    alert(action) {
        Promise.resolve().then(() => action.showAlert()).catch(error => this.log(String(error)));
    }

    press(type, action, rawSettings) {
        const settings = normalizeSettings(type, rawSettings);
        const command = this.client.status === 'ready' ? requestFor(type, settings, this.client.state) : null;
        if (!command || !this.client.request(command, error => {
            this.log(`${error.code}: ${error.message}`);
            this.alert(action);
        })) this.alert(action);
    }

    async refresh(button) {
        button.dirty = true;
        if (button.drawing) return;
        button.drawing = true;
        try {
            while (button.active && button.dirty) {
                button.dirty = false;
                const view = renderButton(button.type, button.settings, this.client);
                const signature = `${view.title}\n${view.svg}`;
                if (signature === button.last) continue;
                await button.action.setImage(view.image);
                if (!button.active) break;
                await button.action.setTitle(view.title);
                button.last = signature;
            }
        } catch (error) {
            this.log(String(error));
        } finally {
            button.drawing = false;
        }
    }
}
