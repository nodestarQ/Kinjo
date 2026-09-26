// The laptop node: receives packets from the radio bridge, opens messages addressed to it
// and seals messages to contacts. Trust comes from the contact list, which is filled from ENS.

import { randomBytes } from '@noble/hashes/utils.js';

import {
	contactUpdate,
	KIND_DRAWING,
	KIND_NOTE,
	KIND_TEXT,
	Reassembler,
	TYPE_SEALED,
	decodeDrawingColored,
	decodeNote,
	notePlaintext,
	encodeDrawing,
	deriveKey,
	nodeId,
	openSealed,
	parsePacket,
	publicKey,
	seal,
	textPlaintext,
	type ColoredStroke,
	type Header
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
	strokes?: ColoredStroke[];
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

/** Everything after the first dot: "alice.kinjo.eth" for "laptop.alice.kinjo.eth". */
const parentName = (name: string) => name.slice(name.indexOf('.') + 1);

/** The laptop's own other devices (same owner, not revoked). They take CONTACT_UPDATE from it. */
export function ownDevices(laptopName: string, contacts: Contact[]): Contact[] {
	if (!laptopName.includes('.')) return [];
	return contacts.filter((c) => c.name !== laptopName && !c.revoked && parentName(c.name) === parentName(laptopName));
}

/**
 * CONTACT_UPDATE plaintexts for what an ENS sync changed (SPEC §11): a revoke for each newly
 * revoked contact, a set for a new key, a contact authorized again or a changed badge.
 */
export function contactUpdates(before: Contact[], after: Contact[]): { name: string; plaintext: Uint8Array }[] {
	const out: { name: string; plaintext: Uint8Array }[] = [];
	for (const c of after) {
		const old = before.find((b) => b.name === c.name);
		if (!old) continue;
		if (c.revoked) {
			if (!old.revoked) out.push({ name: c.name, plaintext: contactUpdate.revoke(c.name) });
		} else if (old.revoked || hex(old.pub) !== hex(c.pub) || !!old.verifiedHuman !== !!c.verifiedHuman) {
			out.push({ name: c.name, plaintext: contactUpdate.set(c.name, c.pub, !!c.verifiedHuman) });
		}
	}
	return out;
}

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
		console.debug('[kinjo mesh] packet for me', header.messageId.toString(16), `frag ${header.fragIndex + 1}/${header.fragCount}`, done ? 'complete' : 'waiting');
		if (done) this.open(done.header, done.body, now);
	}

	private open(header: Header, body: Uint8Array, now: number): void {
		const base = { id: header.messageId, at: now, outgoing: false };
		const contact = this.contacts.find((c) => nodeId(c.pub) === header.source);
		console.debug('[kinjo mesh] opening', header.messageId.toString(16), 'from', contact?.name ?? header.source.toString(16), contact?.revoked ? '(revoked)' : '');
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
		else if (pt[0] === KIND_DRAWING) msg.strokes = decodeDrawingColored(pt);
		else if (pt[0] === KIND_NOTE) {
			const note = decodeNote(pt);
			if (note.text) msg.text = note.text;
			if (note.strokes.length) msg.strokes = note.strokes;
		}
		this.onMessage(msg);
	}

	/** Packets for a sealed text to `to`, ready to send as RADIO_TX. */
	sealText(to: Contact, text: string, now = Date.now()): Uint8Array[] {
		const packets = this.sealTo(to, textPlaintext(text));
		this.onMessage({ id: packets.id, at: now, from: to.name, outgoing: true, status: 'verified', text });
		return packets.packets;
	}

	/** Packets for a sealed drawing to `to`. */
	sealDrawing(to: Contact, strokes: ColoredStroke[], now = Date.now()): Uint8Array[] {
		const packets = this.sealTo(to, encodeDrawing(strokes));
		this.onMessage({ id: packets.id, at: now, from: to.name, outgoing: true, status: 'verified', strokes });
		return packets.packets;
	}

	/** Text and drawing together. Falls back to TEXT or DRAWING when one part is empty. */
	sealNote(to: Contact, text: string, strokes: ColoredStroke[], now = Date.now()): Uint8Array[] {
		if (!strokes.length) return this.sealText(to, text, now);
		if (!text) return this.sealDrawing(to, strokes, now);
		const packets = this.sealTo(to, notePlaintext(text, strokes));
		this.onMessage({ id: packets.id, at: now, from: to.name, outgoing: true, status: 'verified', text, strokes });
		return packets.packets;
	}

	/** Packets for a CONTACT_UPDATE to one of this laptop's devices. Not a chat message. */
	sealUpdate(to: Contact, plaintext: Uint8Array): Uint8Array[] {
		return this.sealTo(to, plaintext).packets;
	}

	private sealTo(to: Contact, plaintext: Uint8Array) {
		const id = new DataView(randomBytes(4).buffer).getUint32(0, true);
		const packets = seal(deriveKey(this.priv, to.pub), this.id, nodeId(to.pub), id, plaintext, randomBytes(12));
		return { id, packets };
	}
}
