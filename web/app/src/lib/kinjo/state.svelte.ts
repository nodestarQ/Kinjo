// App state shared by the pages: the laptop node, the bridge link and the handheld link.

import { bytesToHex } from '@noble/hashes/utils.js';
import type { Address, WalletClient } from 'viem';

import { ensConfig, serialHelperUrl, teamAddress } from './config';
import { DeviceLink } from './device';
import { PARENT, createEns, keyHex } from './ens';
import { loadContacts, loadName, loadPrivateKey, resetPrivateKey, saveContacts, saveName } from './identity';
import { MeshNode, type Contact, type Message, type PacketEvent } from './mesh';
import { onboardingAbi } from './onboarding';
import { FRAME_LOG, FRAME_RADIO_RX, FRAME_RADIO_TX, type ColoredStroke, type DeviceInfo } from './protocol';
import { SerialLink, type HelperPort } from './serial';

const OWNER_KEY = 'kinjo.owner';
const MAX_PACKETS = 100;
const MAX_LOG = 50;
const SYNC_INTERVAL_MS = 10_000;

export interface Owner {
	address: Address;
	/** "alice" once joined, else null. */
	label: string | null;
	verifiedHuman: boolean;
}

class AppState {
	name = $state(loadName());
	contacts = $state<Contact[]>(loadContacts());
	messages = $state<Message[]>([]);
	packets = $state<PacketEvent[]>([]);
	bridgeConnected = $state(false);
	bridgeLog = $state<string[]>([]);
	device = $state<DeviceInfo | null>(null);
	/** Signed-in owner. Cached so the app works offline and survives reloads. */
	owner = $state<Owner | null>(loadOwner());
	online = $state(typeof navigator === 'undefined' ? true : navigator.onLine);
	/** System lines for the chat: registrations, revocations, key changes. */
	notices = $state<{ at: number; text: string }[]>([]);
	/** Full ENS names of the owner's registered devices, read from the chain. */
	myDevices = $state<string[]>([]);
	busy = $state('');
	/** Open port picker (serial helper mode). */
	portChoice = $state<{ ports: HelperPort[]; resolve: (path: string | null) => void } | null>(null);

	readonly ens = createEns(ensConfig);
	private wallet: WalletClient | null = null;
	private walletConnected = $state(false);
	private syncTimer: ReturnType<typeof setInterval> | null = null;

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
			() => (this.bridgeConnected = false),
			serialHelperUrl,
			(ports) => this.pickPort(ports)
		);
		this.deviceLink = new DeviceLink(
			(line) => console.debug('[handheld]', line), // debug output, not shown in the UI
			() => (this.device = null),
			serialHelperUrl,
			(ports) => this.pickPort(ports)
		);
		if (typeof window !== 'undefined') {
			if (this.contacts.length) this.startSync();
			addEventListener('online', () => (this.online = true));
			addEventListener('offline', () => (this.online = false));
			void this.restoreSession();
		}
	}

	get hasWallet(): boolean {
		return this.walletConnected;
	}

	get signedIn(): boolean {
		return !!this.owner?.label;
	}

	/** Reconnects a wallet that already allowed this site and refreshes the owner. Silent. */
	private async restoreSession() {
		try {
			const restored = await this.ens.restoreWallet();
			if (!restored) return;
			if (this.owner && restored.address.toLowerCase() !== this.owner.address.toLowerCase()) return;
			this.wallet = restored.wallet;
			this.walletConnected = true;
			if (this.online) await this.refreshOwner(restored.address);
		} catch {
			// offline or no wallet: keep the cached owner
		}
	}

	signOut() {
		this.wallet = null;
		this.walletConnected = false;
		this.owner = null;
		saveOwner(null);
	}

	private pickPort(ports: HelperPort[]): Promise<string | null> {
		return new Promise((resolve) => {
			this.portChoice = {
				ports,
				resolve: (path) => {
					this.portChoice = null;
					resolve(path);
				}
			};
		});
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

	/** Sends text, drawing or both as one note. */
	async sendNote(to: Contact, text: string, strokes: ColoredStroke[]) {
		for (const p of this.node.sealNote(to, text, strokes)) {
			await this.bridge.send(FRAME_RADIO_TX, p);
			await new Promise((r) => setTimeout(r, 15)); // let the bridge send each fragment
		}
	}

	// --- ENS and onboarding (docs/onboarding.md) ---

	get isTeam(): boolean {
		return this.owner?.address.toLowerCase() === teamAddress;
	}

	/** Runs an ENS action with a status line. Errors are rethrown for the caller to show. */
	private async step<T>(label: string, action: () => Promise<T>): Promise<T> {
		this.busy = label;
		try {
			return await action();
		} finally {
			this.busy = '';
		}
	}

	private notice(text: string) {
		this.notices.push({ at: Date.now(), text });
		if (this.notices.length > MAX_LOG) this.notices.shift();
	}

	async connectWallet() {
		const { wallet, address } = await this.ens.connectWallet();
		this.wallet = wallet;
		this.walletConnected = true;
		await this.refreshOwner(address);
	}

	private async refreshOwner(address: Address) {
		const account = await this.ens.account(address);
		this.owner = { address, label: account?.label ?? null, verifiedHuman: account?.verifiedHuman ?? false };
		saveOwner(this.owner);
		await this.refreshDevices().catch(() => {});
	}

	/** Reads the owner's devices from the chain and makes sure the chat knows each of them. */
	async refreshDevices() {
		const label = this.owner?.label;
		if (!label || !this.owner) return;
		const names = (await this.ens.devicesOf(this.owner.address)).map((d) => `${d}.${label}.${PARENT}`);
		this.myDevices = names;
		for (const name of names) {
			if (name === this.name || this.contacts.some((c) => c.name === name && !c.revoked)) continue;
			this.upsertContact(await this.ens.resolveContact(name).catch(() => null) ?? { name, pub: new Uint8Array(32), revoked: true });
		}
	}

	/** Sign up: claims label.kinjo.eth with this laptop as the first device. */
	async signUp(label: string) {
		await this.join(label, 'laptop', true);
	}

	private requireOwner(): { wallet: WalletClient; owner: Owner } {
		if (!this.wallet || !this.owner) throw new Error('connect a wallet first');
		return { wallet: this.wallet, owner: this.owner };
	}

	/** Claims label.kinjo.eth with the connected handheld (or this laptop) as its first device. */
	async join(label: string, deviceLabel: string, useLaptop: boolean) {
		const { wallet, owner } = this.requireOwner();
		const pub = useLaptop ? this.node.pub : this.device?.pub;
		if (!pub) throw new Error('connect the handheld first');
		if (!(await this.ens.labelFree(label))) throw new Error(`${label}.${PARENT} is taken`);
		await this.step(`Registering ${deviceLabel}.${label}.${PARENT}`, () =>
			this.ens.submit(wallet, { action: 'join', owner: owner.address, label, deviceLabel, deviceKey: keyHex(pub), deadline: this.ens.deadline() })
		);
		await this.refreshOwner(owner.address);
		await this.afterRegister(`${deviceLabel}.${label}.${PARENT}`, useLaptop);
	}

	/** Registers the connected handheld (or this laptop) as another device of the owner. */
	async addDevice(deviceLabel: string, useLaptop: boolean) {
		const { wallet, owner } = this.requireOwner();
		if (!owner.label) throw new Error('claim a name first');
		const pub = useLaptop ? this.node.pub : this.device?.pub;
		if (!pub) throw new Error('connect the handheld first');
		await this.step(`Registering ${deviceLabel}.${owner.label}.${PARENT}`, () =>
			this.ens.submit(wallet, { action: 'addDevice', owner: owner.address, deviceLabel, deviceKey: keyHex(pub), deadline: this.ens.deadline() })
		);
		await this.afterRegister(`${deviceLabel}.${owner.label}.${PARENT}`, useLaptop);
	}

	/** After registering: name the laptop or provision the handheld, and make both know each other. */
	private async afterRegister(name: string, isLaptop: boolean) {
		if (isLaptop) {
			this.setName(name);
			if (this.device) await this.syncDevice();
		} else {
			await this.step('Setting up the handheld', async () => {
				await this.deviceLink.setName(name);
				this.upsertContact(await this.ens.resolveContact(name));
				await this.pushContactsToDevice();
			});
		}
		this.notice(`${name} registered in ENS`);
		await this.refreshDevices().catch(() => {});
	}

	/** Pushes this laptop and every contact from ENS to the connected handheld. */
	async pushContactsToDevice() {
		if (!this.device) throw new Error('connect the handheld first');
		if (!this.name) throw new Error('register this laptop first (it needs an ENS name)');
		const own = (await this.deviceLink.info()).name;
		await this.deviceLink.clearContacts();
		await this.deviceLink.addContact(this.name, this.node.pub, !!this.owner?.verifiedHuman);
		for (const c of this.contacts) {
			if (c.name !== own && c.name !== this.name && !c.revoked) {
				await this.deviceLink.addContact(c.name, c.pub, !!c.verifiedHuman);
			}
		}
		this.device = await this.deviceLink.info();
	}

	/** Connects the handheld over USB and brings it up to date with ENS when signed in. */
	async connectDevice() {
		this.device = await this.deviceLink.connect();
		if (this.online && this.owner?.label) await this.syncDevice();
	}

	/**
	 * Brings the connected handheld up to date with ENS: the name its key has now (after a rename
	 * done while it was unplugged) and the current contacts, e.g. this laptop's new name.
	 */
	async syncDevice() {
		if (!this.device || !this.owner?.label) return;
		await this.refreshDevices();
		const key = keyHex(this.device.pub);
		const name = this.myDevices.find((n) =>
			this.contacts.some((c) => c.name === n && !c.revoked && keyHex(c.pub) === key)
		);
		if (name && name !== this.device.name) {
			await this.step('Updating the handheld name', () => this.deviceLink.setName(name));
			this.notice(`handheld now shows ${name}`);
		}
		if (this.name) await this.step('Updating the handheld contacts', () => this.pushContactsToDevice());
		else this.device = await this.deviceLink.info();
	}

	async revoke(deviceLabel: string, stepLabel = '') {
		const { wallet, owner } = this.requireOwner();
		const name = `${deviceLabel}.${owner.label}.${PARENT}`;
		const renaming = !!stepLabel;
		await this.step(stepLabel || `Revoking ${name}`, () =>
			this.ens.submit(wallet, { action: 'revokeDevice', owner: owner.address, deviceLabel, deadline: this.ens.deadline() })
		);
		this.notice(`${name} revoked in ENS`);
		// This laptop is no longer a device. A rename sets the new name right after.
		if (name === this.name && !renaming) this.setName('');
		await this.syncEns();
		await this.refreshDevices().catch(() => {});
	}

	/**
	 * Renames a device: registers the new name with the same key, then revokes the old one.
	 * ENSv2 has no rename for subnames. Contacts keep working because the key stays the same.
	 */
	async renameDevice(oldName: string, newLabel: string) {
		const { wallet, owner } = this.requireOwner();
		const isLaptop = oldName === this.name;
		const pub = isLaptop ? this.node.pub : this.contacts.find((c) => c.name === oldName)?.pub;
		if (!pub) throw new Error(`no key known for ${oldName}`);
		const newName = `${newLabel}.${owner.label}.${PARENT}`;
		await this.step(`Signature 1 of 2: registering ${newName}`, () =>
			this.ens.submit(wallet, { action: 'addDevice', owner: owner.address, deviceLabel: newLabel, deviceKey: keyHex(pub), deadline: this.ens.deadline() })
		);
		await this.revoke(oldName.split('.')[0], `Signature 2 of 2: revoking ${oldName}`);
		if (isLaptop) {
			this.setName(newName);
		} else {
			this.removeContact(oldName);
			this.upsertContact(await this.ens.resolveContact(newName));
		}
		this.notice(`${oldName} is now ${newName}`);
		// A plugged-in handheld gets its new name or the laptop's new name right away.
		// Otherwise it catches up the next time it's connected.
		if (this.device) await this.syncDevice();
		else await this.refreshDevices().catch(() => {});
	}

	async addContactByName(name: string) {
		this.upsertContact(await this.ens.resolveContact(name.trim().toLowerCase()));
		this.startSync();
	}

	/** Re-reads every contact from ENS. Revoked devices get rejected from now on. */
	async syncEns() {
		if (!this.contacts.length) return;
		const { contacts, changes } = await this.ens.sync($state.snapshot(this.contacts) as typeof this.contacts);
		this.contacts = contacts;
		this.node.contacts = this.contacts;
		saveContacts(this.contacts);
		for (const c of changes) this.notice(c);
		if (this.owner) await this.refreshOwner(this.owner.address);
	}

	startSync() {
		if (this.syncTimer) return;
		this.syncTimer = setInterval(() => this.syncEns().catch(() => {}), SYNC_INTERVAL_MS);
	}

	/** Demo reset: revoke the handheld in ENS, wipe it, forget the wallet session. */
	async resetDemo(deviceLabel: string) {
		if (this.owner?.label) await this.revoke(deviceLabel).catch((e) => this.notice(`revoke failed: ${(e as Error).message}`));
		if (this.device) this.device = await this.step('Wiping the handheld', () => this.deviceLink.wipe());
		this.notice('demo reset done');
		this.signOut();
	}

	/** Team only: frees label.kinjo.eth (and its badge) so it can be claimed again. */
	async release(label: string) {
		const { wallet } = this.requireOwner();
		if (!this.isTeam) throw new Error('only the kinjo.eth wallet can release names');
		await this.step(`Releasing ${label}.${PARENT}`, async () => {
			const hash = await wallet.writeContract({
				account: wallet.account ?? this.owner!.address,
				chain: this.ens.chain,
				address: ensConfig.onboarding,
				abi: onboardingAbi,
				functionName: 'release',
				args: [label]
			});
			await this.ens.client.waitForTransactionReceipt({ hash });
		});
		this.notice(`${label}.${PARENT} released`);
	}

	/** New laptop key. Everyone who knew the old one must be updated (ENS record, handheld contacts). */
	resetKey() {
		resetPrivateKey();
		location.reload();
	}
}

function loadOwner(): Owner | null {
	try {
		return JSON.parse(localStorage.getItem(OWNER_KEY) ?? 'null');
	} catch {
		return null;
	}
}

function saveOwner(owner: Owner | null) {
	try {
		if (owner) localStorage.setItem(OWNER_KEY, JSON.stringify(owner));
		else localStorage.removeItem(OWNER_KEY);
	} catch {
		// private mode: session lives only in this tab
	}
}

export const app = new AppState();
