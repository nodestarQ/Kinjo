// Talks to a handheld over USB: one PROVISION request at a time, each answered by one reply (SPEC §13).

import {
	FRAME_LOG,
	FRAME_PROVISION,
	FRAME_PROVISION_REPLY,
	STATUS_OK,
	parseInfo,
	parseProvisionReply,
	provision,
	type DeviceInfo
} from './protocol';
import { SerialLink } from './serial';

const STATUS_TEXT = ['OK', 'malformed request', 'contact list full', 'unknown command'];
const REPLY_TIMEOUT_MS = 3000;

export class DeviceLink {
	private link: SerialLink;
	private pending: { resolve: (data: Uint8Array) => void; reject: (e: Error) => void; command: number } | null = null;

	constructor(onLog: (line: string) => void, onClose: () => void) {
		this.link = new SerialLink((type, body) => {
			if (type === FRAME_LOG) onLog(new TextDecoder().decode(body));
			if (type === FRAME_PROVISION_REPLY) this.handleReply(body);
		}, onClose);
	}

	get connected(): boolean {
		return this.link.connected;
	}

	async connect(): Promise<DeviceInfo> {
		await this.link.connect();
		await new Promise((r) => setTimeout(r, 1500)); // the board resets when the port opens
		return this.info();
	}

	disconnect(): Promise<void> {
		return this.link.disconnect();
	}

	async info(): Promise<DeviceInfo> {
		return parseInfo(await this.request(provision.info()));
	}

	async setName(name: string): Promise<void> {
		await this.request(provision.setName(name));
	}

	async addContact(name: string, pub: Uint8Array, verified: boolean): Promise<void> {
		await this.request(provision.addContact(name, pub, verified));
	}

	async clearContacts(): Promise<void> {
		await this.request(provision.clearContacts());
	}

	/** New key pair, name and contacts cleared. Returns the new info. */
	async wipe(): Promise<DeviceInfo> {
		return parseInfo(await this.request(provision.wipe()));
	}

	private request(body: Uint8Array): Promise<Uint8Array> {
		if (this.pending) return Promise.reject(new Error('another request is still running'));
		return new Promise((resolve, reject) => {
			const timer = setTimeout(() => {
				this.pending = null;
				reject(new Error('the device did not answer'));
			}, REPLY_TIMEOUT_MS);
			this.pending = {
				command: body[0],
				resolve: (d) => (clearTimeout(timer), resolve(d)),
				reject: (e) => (clearTimeout(timer), reject(e))
			};
			this.link.send(FRAME_PROVISION, body).catch((e) => {
				this.pending = null;
				clearTimeout(timer);
				reject(e);
			});
		});
	}

	private handleReply(body: Uint8Array): void {
		const p = this.pending;
		if (!p) return;
		this.pending = null;
		try {
			const { command, status, data } = parseProvisionReply(body);
			if (command !== p.command) throw new Error('reply to a different command');
			if (status !== STATUS_OK) throw new Error(STATUS_TEXT[status] ?? `status ${status}`);
			p.resolve(data);
		} catch (e) {
			p.reject(e as Error);
		}
	}
}
