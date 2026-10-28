import streamDeck, { SingletonAction, action } from '@elgato/streamdeck';
import { ACTION_TYPES } from './protocol.mjs';
import { MixerClient } from './mixer-client.mjs';
import { ButtonController } from './controller.mjs';

const client = new MixerClient();
const controller = new ButtonController(client, message => streamDeck.logger.warn(message));

class MixerAction extends SingletonAction {
    constructor(type) {
        super();
        this.type = type;
    }

    onWillAppear(event) {
        if (event.action.isKey()) controller.appear(this.type, event.action, event.payload.settings);
    }

    onWillDisappear(event) {
        controller.disappear(event.action.id);
    }

    onDidReceiveSettings(event) {
        if (event.action.isKey()) controller.settings(this.type, event.action, event.payload.settings);
    }

    onKeyDown(event) {
        controller.press(this.type, event.action, event.payload.settings);
    }
}

for (const type of ACTION_TYPES) {
    const ActionClass = action({ UUID: `site.klinglab.streammixer.${type}` })(MixerAction);
    streamDeck.actions.registerAction(new ActionClass(type));
}

streamDeck.connect().then(() => client.start()).catch(error => streamDeck.logger.error(String(error)));
for (const signal of ['SIGINT', 'SIGTERM']) {
    process.once(signal, () => {
        client.stop();
        process.exit(0);
    });
}
