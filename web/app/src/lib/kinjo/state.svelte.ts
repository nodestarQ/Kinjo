// App state shared by the pages: the laptop node, the bridge link and the handheld link.

import { bytesToHex } from '@noble/hashes/utils.js';

import { DeviceLink } from './device';
import { loadContacts, loadName, loadPrivateKey, resetPrivateKey, saveContacts, saveName } from './identity';
import { MeshNode, type Contact, type Message, type PacketEvent } from './mesh';
import { FRAME_LOG, FRAME_RADIO_RX, FRAME_RADIO_TX, type DeviceInfo } from './protocol';
import { SerialLink } from './serial';

const MAX_PACKETS = 100;
const MAX_LOG = 50;

class AppState {
	name = $state(loadName());
	contacts = $state<Contact[]>(loadContacts());
	messages = $state<Message[]>([]);
	packets = $state<PacketEvent[]>([]);
	bridgeConnected = $state(false);
	bridgeLog = $state<string[]>([]);
	device = $state<DeviceInfo | null>(null);
	deviceLog = $state<string[]>([]);

	node: MeshNode;
	pubHex: string;
	private bridge: SerialLink;
	readonly deviceLink: DeviceLink;

	constructor() {
		this.node = new MeshNode(
			loadPrivateKey(),
			(m) => this.messages.push(m),
			(p) => {
				this.packets.unshift(p);
				if (this.packets.length > MAX_PACKETS) this.packets.pop();
			}
		);
		this.node.contacts = this.contacts;
		this.pubHex = bytesToHex(this.node.pub);
		this.bridge = new SerialLink(
			(type, body) => {
				if (type === FRAME_RADIO_RX) this.node.handleRadioRx(body);
				else if (type === FRAME_LOG) this.pushLog(this.bridgeLog, new TextDecoder().decode(body));
			},
			() => (this.bridgeConnected = false)
		);
		this.deviceLink = new DeviceLink(
			(line) => this.pushLog(this.deviceLog, line),
			() => (this.device = null)
		);
	}

	private pushLog(log: string[], line: string) {
		log.unshift(line);
		if (log.length > MAX_LOG) log.pop();
	}

	setName(name: string) {
		this.name = name;
		saveName(name);
	}

	/** Adds or replaces a contact by name. */
	upsertContact(contact: Contact) {
		const i = this.contacts.findIndex((c) => c.name === contact.name);
		if (i >= 0) this.contacts[i] = contact;
		else this.contacts.push(contact);
		this.node.contacts = this.contacts;
		saveContacts(this.contacts);
	}

	removeContact(name: string) {
		this.contacts = this.contacts.filter((c) => c.name !== name);
		this.node.contacts = this.contacts;
		saveContacts(this.contacts);
	}

	async connectBridge() {
		await this.bridge.connect();
		this.bridgeConnected = true;
	}

	async disconnectBridge() {
		await this.bridge.disconnect();
	}

	async sendText(to: Contact, text: string) {
		for (const p of this.node.sealText(to, text)) await this.bridge.send(FRAME_RADIO_TX, p);
	}

	/** New laptop key. Everyone who knew the old one must be updated (ENS record, handheld contacts). */
	resetKey() {
		resetPrivateKey();
		location.reload();
	}
}

export const app = new AppState();
