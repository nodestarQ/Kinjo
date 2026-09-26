// App state shared by the pages: the laptop node, the bridge link and the handheld link.

import { bytesToHex } from '@noble/hashes/utils.js';
import type { Address, WalletClient } from 'viem';

import { ensConfig, teamAddress } from './config';
import { DeviceLink } from './device';
import { PARENT, createEns, keyHex } from './ens';
import { loadContacts, loadName, loadPrivateKey, resetPrivateKey, saveContacts, saveName } from './identity';
import { MeshNode, type Contact, type Message, type PacketEvent } from './mesh';
import { onboardingAbi } from './onboarding';
import { FRAME_LOG, FRAME_RADIO_RX, FRAME_RADIO_TX, type DeviceInfo } from './protocol';
import { SerialLink } from './serial';

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
	deviceLog = $state<string[]>([]);
	owner = $state<Owner | null>(null);
	/** What the last ENS syncs changed, newest first. */
	notices = $state<string[]>([]);
	busy = $state('');

	readonly ens = createEns(ensConfig);
	private wallet: WalletClient | null = null;
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
			() => (this.bridgeConnected = false)
		);
		this.deviceLink = new DeviceLink(
			(line) => this.pushLog(this.deviceLog, line),
			() => (this.device = null)
		);
		if (typeof window !== 'undefined' && this.contacts.length) this.startSync();
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

	private notice(line: string) {
		this.pushLog(this.notices, `${new Date().toLocaleTimeString()} ${line}`);
	}

	async connectWallet() {
		const { wallet, address } = await this.ens.connectWallet();
		this.wallet = wallet;
		await this.refreshOwner(address);
	}

	private async refreshOwner(address: Address) {
		const account = await this.ens.account(address);
		this.owner = { address, label: account?.label ?? null, verifiedHuman: account?.verifiedHuman ?? false };
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
		} else {
			await this.step('Setting up the handheld', async () => {
				await this.deviceLink.setName(name);
				this.upsertContact(await this.ens.resolveContact(name));
				await this.pushContactsToDevice();
			});
		}
		this.notice(`${name} registered in ENS`);
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

	async revoke(deviceLabel: string) {
		const { wallet, owner } = this.requireOwner();
		await this.step(`Revoking ${deviceLabel}.${owner.label}.${PARENT}`, () =>
			this.ens.submit(wallet, { action: 'revokeDevice', owner: owner.address, deviceLabel, deadline: this.ens.deadline() })
		);
		this.notice(`${deviceLabel}.${owner.label}.${PARENT} revoked in ENS`);
		await this.syncEns();
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
		this.wallet = null;
		this.owner = null;
		this.notice('demo reset done');
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

export const app = new AppState();
