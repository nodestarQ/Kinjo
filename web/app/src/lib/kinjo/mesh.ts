// The laptop node: receives packets from the radio bridge, opens messages addressed to it
// and seals messages to contacts. Trust comes from the contact list, which is filled from ENS.

import { randomBytes } from '@noble/hashes/utils.js';

import {
	KIND_DRAWING,
	KIND_TEXT,
	Reassembler,
	TYPE_SEALED,
	decodeDrawing,
	deriveKey,
	nodeId,
	openSealed,
	parsePacket,
	publicKey,
	seal,
	textPlaintext,
	type Header,
	type Stroke
} from './protocol';

export interface Contact {
	name: string;
	pub: Uint8Array;
	verifiedHuman?: boolean;
	/** Set when the key is no longer in ENS. Messages are rejected. */
	revoked?: boolean;
}

export type MessageStatus = 'verified' | 'revoked' | 'unknown sender' | 'failed to open';

export interface Message {
	id: number;
	at: number;
	from: string;
	outgoing: boolean;
	status: MessageStatus;
	text?: string;
	strokes?: Stroke[];
}

export interface PacketEvent {
	at: number;
	fromMac: string;
	header: Header;
	size: number;
	forMe: boolean;
	preview: string;
}

const SEEN_MAX = 256;

const hex = (b: Uint8Array) => [...b].map((x) => x.toString(16).padStart(2, '0')).join('');

export class MeshNode {
	readonly pub: Uint8Array;
	readonly id: number;
	contacts: Contact[] = [];
	private seen: string[] = [];
	private reassembler = new Reassembler();

	constructor(
		private priv: Uint8Array,
		private onMessage: (m: Message) => void,
		private onPacket: (p: PacketEvent) => void = () => {}
	) {
		this.pub = publicKey(priv);
		this.id = nodeId(this.pub);
	}

	/** Handles a RADIO_RX body: from MAC (6) + RSSI (1) + packet. */
	handleRadioRx(body: Uint8Array, now = Date.now()): void {
		if (body.length < 8) return;
		const fromMac = [...body.subarray(0, 6)].map((b) => b.toString(16).padStart(2, '0')).join(':');
		const packet = body.subarray(7);
		let parsed;
		try {
			parsed = parsePacket(packet);
		} catch {
			return;
		}
		const { header, fragment } = parsed;
		const key = `${header.source}:${header.messageId}:${header.fragIndex}`;
		if (this.seen.includes(key)) return;
		this.seen.push(key);
		if (this.seen.length > SEEN_MAX) this.seen.shift();

		const forMe = header.destination === this.id;
		this.onPacket({ at: now, fromMac, header, size: packet.length, forMe, preview: hex(fragment.subarray(0, 16)) });
		if (!forMe || header.type !== TYPE_SEALED) return;

		const done = this.reassembler.push(packet, now);
		if (done) this.open(done.header, done.body, now);
	}

	private open(header: Header, body: Uint8Array, now: number): void {
		const base = { id: header.messageId, at: now, outgoing: false };
		const contact = this.contacts.find((c) => nodeId(c.pub) === header.source);
		if (!contact) {
			this.onMessage({ ...base, from: header.source.toString(16).padStart(8, '0'), status: 'unknown sender' });
			return;
		}
		if (contact.revoked) {
			this.onMessage({ ...base, from: contact.name, status: 'revoked' });
			return;
		}
		let pt: Uint8Array;
		try {
			pt = openSealed(deriveKey(this.priv, contact.pub), header, body);
		} catch {
			this.onMessage({ ...base, from: contact.name, status: 'failed to open' });
			return;
		}
		const msg: Message = { ...base, from: contact.name, status: 'verified' };
		if (pt[0] === KIND_TEXT) msg.text = new TextDecoder().decode(pt.subarray(1));
		else if (pt[0] === KIND_DRAWING) msg.strokes = decodeDrawing(pt);
		this.onMessage(msg);
	}

	/** Packets for a sealed text to `to`, ready to send as RADIO_TX. */
	sealText(to: Contact, text: string, now = Date.now()): Uint8Array[] {
		const messageId = new DataView(randomBytes(4).buffer).getUint32(0, true);
		const packets = seal(deriveKey(this.priv, to.pub), this.id, nodeId(to.pub), messageId, textPlaintext(text), randomBytes(12));
		this.onMessage({ id: messageId, at: now, from: to.name, outgoing: true, status: 'verified', text });
		return packets;
	}
}
