// Kinjo wire format v0.1 (protocol/SPEC.md) for the browser. Tested against protocol/test-vectors.

import { chacha20poly1305 } from '@noble/ciphers/chacha.js';
import { x25519 } from '@noble/curves/ed25519.js';
import { hkdf } from '@noble/hashes/hkdf.js';
import { sha512 } from '@noble/hashes/sha2.js';

export const VERSION = 0x01;
export const TYPE_IDENTITY = 0x01;
export const TYPE_SEALED = 0x10;
export const KIND_TEXT = 0x01;
export const KIND_DRAWING = 0x02;
export const KIND_NOTE = 0x03;
export const KIND_CONTACT_UPDATE = 0x04;
export const OP_SET = 0x01;
export const OP_REVOKE = 0x02;
export const BROADCAST = 0xffffffff;
export const DEFAULT_TTL = 4;

export const HEADER_SIZE = 18;
export const FRAGMENT_MAX = 232;
export const MAX_FRAGMENTS = 16;
export const MAX_BODY = FRAGMENT_MAX * MAX_FRAGMENTS;
export const NONCE_SIZE = 12;
export const TAG_SIZE = 16;
export const KEY_SIZE = 32;
export const MAX_TEXT = 200;
export const MAX_NAME = 64;
export const REASSEMBLY_TIMEOUT_MS = 5000;

export const FRAME_RADIO_RX = 0x01;
export const FRAME_RADIO_TX = 0x02;
export const FRAME_LOG = 0x03;
export const FRAME_PROVISION = 0x04;
export const FRAME_PROVISION_REPLY = 0x05;
export const FRAME_SEND_TEXT = 0x06;

export const CMD_INFO = 0x01;
export const CMD_SET_NAME = 0x02;
export const CMD_ADD_CONTACT = 0x03;
export const CMD_CLEAR_CONTACTS = 0x04;
export const CMD_WIPE = 0x05;
export const STATUS_OK = 0x00;
export const FLAG_VERIFIED = 0x01;

export class ProtocolError extends Error {}

const utf8 = new TextEncoder();
const fromUtf8 = new TextDecoder();

export function concat(...parts: Uint8Array[]): Uint8Array {
	const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
	let i = 0;
	for (const p of parts) {
		out.set(p, i);
		i += p.length;
	}
	return out;
}

// --- Header (SPEC §5) ---

export interface Header {
	version: number;
	type: number;
	flags: number;
	ttl: number;
	messageId: number;
	source: number;
	destination: number;
	fragIndex: number;
	fragCount: number;
}

export function encodeHeader(h: Header): Uint8Array {
	const b = new Uint8Array(HEADER_SIZE);
	const v = new DataView(b.buffer);
	b[0] = h.version;
	b[1] = h.type;
	b[2] = h.flags;
	b[3] = h.ttl;
	v.setUint32(4, h.messageId, true);
	v.setUint32(8, h.source, true);
	v.setUint32(12, h.destination, true);
	b[16] = h.fragIndex;
	b[17] = h.fragCount;
	return b;
}

export function decodeHeader(b: Uint8Array): Header {
	if (b.length < HEADER_SIZE) throw new ProtocolError('short header');
	const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
	return {
		version: b[0],
		type: b[1],
		flags: b[2],
		ttl: b[3],
		messageId: v.getUint32(4, true),
		source: v.getUint32(8, true),
		destination: v.getUint32(12, true),
		fragIndex: b[16],
		fragCount: b[17]
	};
}

/** AEAD associated data: header bytes 0-2, 4-15 and 17. */
export function headerAd(h: Header): Uint8Array {
	const raw = encodeHeader(h);
	return concat(raw.subarray(0, 3), raw.subarray(4, 16), raw.subarray(17, 18));
}

export function parsePacket(packet: Uint8Array): { header: Header; fragment: Uint8Array } {
	if (packet.length < HEADER_SIZE + 1) throw new ProtocolError('packet too short');
	const header = decodeHeader(packet);
	if (header.version !== VERSION) throw new ProtocolError('unknown version');
	if (header.fragCount < 1 || header.fragCount > MAX_FRAGMENTS || header.fragIndex >= header.fragCount) {
		throw new ProtocolError('bad fragment fields');
	}
	return { header, fragment: packet.subarray(HEADER_SIZE) };
}

// --- Fragmentation (SPEC §7) ---

export function fragCountFor(bodyLen: number): number {
	if (bodyLen < 1 || bodyLen > MAX_BODY) throw new ProtocolError(`body length ${bodyLen} out of range`);
	return Math.ceil(bodyLen / FRAGMENT_MAX);
}

export function fragment(
	type: number,
	messageId: number,
	source: number,
	destination: number,
	body: Uint8Array,
	ttl = DEFAULT_TTL
): Uint8Array[] {
	const fragCount = fragCountFor(body.length);
	const packets: Uint8Array[] = [];
	for (let i = 0; i < fragCount; i++) {
		const h: Header = { version: VERSION, type, flags: 0, ttl, messageId, source, destination, fragIndex: i, fragCount };
		packets.push(concat(encodeHeader(h), body.subarray(i * FRAGMENT_MAX, (i + 1) * FRAGMENT_MAX)));
	}
	return packets;
}

/** Collects fragments per (source, messageId). */
export class Reassembler {
	private slots = new Map<string, { started: number; header: Header; parts: Map<number, Uint8Array> }>();

	push(packet: Uint8Array, nowMs: number): { header: Header; body: Uint8Array } | null {
		const { header, fragment: frag } = parsePacket(packet);
		for (const [k, s] of this.slots) if (nowMs - s.started > REASSEMBLY_TIMEOUT_MS) this.slots.delete(k);
		if (header.fragCount === 1) return { header, body: frag.slice() };
		const key = `${header.source}:${header.messageId}`;
		let slot = this.slots.get(key);
		if (!slot) {
			slot = { started: nowMs, header, parts: new Map() };
			this.slots.set(key, slot);
		} else if (slot.header.fragCount !== header.fragCount) {
			this.slots.delete(key);
			return null;
		}
		slot.parts.set(header.fragIndex, frag.slice());
		if (slot.parts.size < header.fragCount) return null;
		this.slots.delete(key);
		const parts = [...Array(header.fragCount).keys()].map((i) => slot.parts.get(i)!);
		return { header: slot.header, body: concat(...parts) };
	}
}

// --- Identity and crypto (SPEC §3, §10) ---

export function publicKey(priv: Uint8Array): Uint8Array {
	return x25519.getPublicKey(priv);
}

/** First 4 bytes of the public key, little-endian. */
export function nodeId(pub: Uint8Array): number {
	return new DataView(pub.buffer, pub.byteOffset, 4).getUint32(0, true);
}

function compareBytes(a: Uint8Array, b: Uint8Array): number {
	for (let i = 0; i < Math.min(a.length, b.length); i++) if (a[i] !== b[i]) return a[i] - b[i];
	return a.length - b.length;
}

export function sharedSecret(priv: Uint8Array, peerPub: Uint8Array): Uint8Array {
	return x25519.getSharedSecret(priv, peerPub);
}

export function deriveKey(priv: Uint8Array, peerPub: Uint8Array): Uint8Array {
	const own = publicKey(priv);
	const [lo, hi] = compareBytes(own, peerPub) < 0 ? [own, peerPub] : [peerPub, own];
	const info = concat(utf8.encode('kinjo/0.1'), lo, hi);
	return hkdf(sha512, sharedSecret(priv, peerPub), new Uint8Array(0), info, KEY_SIZE);
}

/** Encrypts a SEALED message and splits it into packets. */
export function seal(
	key: Uint8Array,
	source: number,
	destination: number,
	messageId: number,
	plaintext: Uint8Array,
	nonce: Uint8Array,
	ttl = DEFAULT_TTL
): Uint8Array[] {
	if (nonce.length !== NONCE_SIZE) throw new ProtocolError('nonce must be 12 bytes');
	const fragCount = fragCountFor(NONCE_SIZE + plaintext.length + TAG_SIZE);
	const h: Header = { version: VERSION, type: TYPE_SEALED, flags: 0, ttl, messageId, source, destination, fragIndex: 0, fragCount };
	const body = concat(nonce, chacha20poly1305(key, nonce, headerAd(h)).encrypt(plaintext));
	return fragment(TYPE_SEALED, messageId, source, destination, body, ttl);
}

/** Decrypts a reassembled SEALED body. Throws on any tampering. */
export function openSealed(key: Uint8Array, header: Header, body: Uint8Array): Uint8Array {
	if (header.type !== TYPE_SEALED || body.length < NONCE_SIZE + TAG_SIZE + 1) throw new ProtocolError('not a sealed body');
	const nonce = body.subarray(0, NONCE_SIZE);
	return chacha20poly1305(key, nonce, headerAd(header)).decrypt(body.subarray(NONCE_SIZE));
}

// --- Payloads (SPEC §6, §9) ---

export function textPlaintext(text: string): Uint8Array {
	const data = utf8.encode(text);
	if (data.length > MAX_TEXT) throw new ProtocolError('text too long');
	return concat(new Uint8Array([KIND_TEXT]), data);
}

export type Stroke = [number, number][];
export interface ColoredStroke {
	/** Index into PALETTE. */
	color: number;
	points: Stroke;
}

/** Pen colors (SPEC §9). */
export const PALETTE = ['#26313d', '#d8453b', '#e0832f', '#d9b92b', '#2f9e5b', '#3d6fd6', '#8a57d6', '#d4548e'];
const COLOR_MARKER = 0x00;

export function decodeDrawingColored(pt: Uint8Array): ColoredStroke[] {
	if (!pt.length || pt[0] !== KIND_DRAWING) throw new ProtocolError('not a drawing');
	return decodeItems(pt, 1);
}

/** NOTE: text and drawing in one message (SPEC §6). */
export function notePlaintext(text: string, strokes: ColoredStroke[]): Uint8Array {
	const t = utf8.encode(text);
	if (t.length > MAX_TEXT) throw new ProtocolError('text too long');
	return concat(new Uint8Array([KIND_NOTE, t.length]), t, encodeDrawing(strokes).subarray(1));
}

export function decodeNote(pt: Uint8Array): { text: string; strokes: ColoredStroke[] } {
	if (pt.length < 2 || pt[0] !== KIND_NOTE) throw new ProtocolError('not a note');
	const n = pt[1];
	if (n > MAX_TEXT || 2 + n > pt.length) throw new ProtocolError('bad note text');
	return { text: fromUtf8.decode(pt.subarray(2, 2 + n)), strokes: decodeItems(pt, 2 + n) };
}

function decodeItems(pt: Uint8Array, start: number): ColoredStroke[] {
	const v = new DataView(pt.buffer, pt.byteOffset, pt.byteLength);
	const strokes: ColoredStroke[] = [];
	let i = start;
	let color = 0;
	while (i < pt.length) {
		const count = pt[i];
		if (count === COLOR_MARKER) {
			if (i + 1 >= pt.length || pt[i + 1] >= PALETTE.length) throw new ProtocolError('bad color');
			color = pt[i + 1];
			i += 2;
			continue;
		}
		if (i + 5 + 2 * (count - 1) > pt.length) throw new ProtocolError('bad stroke');
		let x = v.getUint16(i + 1, true);
		let y = v.getUint16(i + 3, true);
		i += 5;
		const stroke: Stroke = [[x, y]];
		for (let k = 1; k < count; k++) {
			x += v.getInt8(i);
			y += v.getInt8(i + 1);
			i += 2;
			stroke.push([x, y]);
		}
		strokes.push({ color, points: stroke });
	}
	return strokes;
}

export const CANVAS_W = 320;
export const CANVAS_H = 240;

/** Points after `a` up to `b`, each step at most 127 on both axes (like the reference). */
function steps(a: [number, number], b: [number, number]): [number, number][] {
	const dx = b[0] - a[0];
	const dy = b[1] - a[1];
	const n = Math.max(1, Math.ceil(Math.max(Math.abs(dx), Math.abs(dy)) / 127));
	const out: [number, number][] = [];
	for (let i = 1; i <= n; i++) out.push([a[0] + Math.floor((dx * i) / n), a[1] + Math.floor((dy * i) / n)]);
	return out;
}

/** DRAWING plaintext. Big jumps get intermediate points, long strokes get split, colors only when they change. */
export function encodeDrawing(strokes: ColoredStroke[]): Uint8Array {
	const out: number[] = [KIND_DRAWING];
	let current = 0;
	for (const { color, points: stroke } of strokes) {
		if (!stroke.length) continue;
		if (color < 0 || color >= PALETTE.length) throw new ProtocolError(`color ${color} not in the palette`);
		if (color !== current) {
			out.push(COLOR_MARKER, color);
			current = color;
		}
		for (const [x, y] of stroke) {
			if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H) throw new ProtocolError(`point ${x},${y} off canvas`);
		}
		let points: [number, number][] = [stroke[0]];
		for (const p of stroke.slice(1)) points = points.concat(steps(points[points.length - 1], p));
		while (points.length) {
			const chunk = points.slice(0, 255);
			points = points.length > 255 ? points.slice(254) : [];
			out.push(chunk.length, chunk[0][0] & 0xff, chunk[0][0] >> 8, chunk[0][1] & 0xff, chunk[0][1] >> 8);
			for (let i = 1; i < chunk.length; i++) {
				out.push((chunk[i][0] - chunk[i - 1][0]) & 0xff, (chunk[i][1] - chunk[i - 1][1]) & 0xff);
			}
		}
	}
	return new Uint8Array(out);
}

/** Strokes without their colors. */
export const decodeDrawing = (pt: Uint8Array): Stroke[] => decodeDrawingColored(pt).map((s) => s.points);

// --- Serial framing (SPEC §12) ---

export function cobsEncode(data: Uint8Array): Uint8Array {
	const out: number[] = [0];
	let codePos = 0;
	let code = 1;
	for (const b of data) {
		if (b !== 0) {
			out.push(b);
			if (++code !== 0xff) continue;
		}
		out[codePos] = code;
		codePos = out.length;
		out.push(0);
		code = 1;
	}
	out[codePos] = code;
	return new Uint8Array(out);
}

export function cobsDecode(data: Uint8Array): Uint8Array {
	const out: number[] = [];
	let i = 0;
	while (i < data.length) {
		const code = data[i];
		if (code === 0 || i + code > data.length) throw new ProtocolError('bad COBS');
		for (let k = 1; k < code; k++) {
			if (data[i + k] === 0) throw new ProtocolError('bad COBS');
			out.push(data[i + k]);
		}
		i += code;
		if (code !== 0xff && i < data.length) out.push(0);
	}
	return new Uint8Array(out);
}

export function serialFrame(type: number, body: Uint8Array): Uint8Array {
	return concat(cobsEncode(concat(new Uint8Array([type]), body)), new Uint8Array([0]));
}

/** Splits a serial byte stream into frames. Boot text and broken frames are skipped. */
export class FrameReader {
	private buf: number[] = [];

	push(chunk: Uint8Array): { type: number; body: Uint8Array }[] {
		const frames: { type: number; body: Uint8Array }[] = [];
		for (const b of chunk) {
			if (b !== 0) {
				if (this.buf.length < 4096) this.buf.push(b);
				continue;
			}
			const raw = new Uint8Array(this.buf);
			this.buf = [];
			if (!raw.length) continue;
			try {
				const data = cobsDecode(raw);
				if (data.length) frames.push({ type: data[0], body: data.subarray(1) });
			} catch {
				// boot text from the ESP32 ROM
			}
		}
		return frames;
	}
}

// --- Provisioning (SPEC §13) ---

export function encodeName(name: string): Uint8Array {
	const n = utf8.encode(name);
	if (n.length > MAX_NAME) throw new ProtocolError('name too long');
	return concat(new Uint8Array([n.length]), n);
}

export function decodeName(data: Uint8Array, offset: number): [string, number] {
	if (offset >= data.length) throw new ProtocolError('missing name');
	const n = data[offset];
	const end = offset + 1 + n;
	if (n > MAX_NAME || end > data.length) throw new ProtocolError('bad name');
	return [fromUtf8.decode(data.subarray(offset + 1, end)), end];
}

/** CONTACT_UPDATE plaintext (SPEC §11): SET adds or replaces a contact, REVOKE marks it revoked. */
export const contactUpdate = {
	set: (name: string, pub: Uint8Array, verified: boolean) =>
		concat(new Uint8Array([KIND_CONTACT_UPDATE, OP_SET, verified ? FLAG_VERIFIED : 0]), pub, encodeName(name)),
	revoke: (name: string) => concat(new Uint8Array([KIND_CONTACT_UPDATE, OP_REVOKE, 0]), new Uint8Array(KEY_SIZE), encodeName(name))
};

export const provision = {
	info: () => new Uint8Array([CMD_INFO]),
	setName: (name: string) => concat(new Uint8Array([CMD_SET_NAME]), encodeName(name)),
	addContact: (name: string, pub: Uint8Array, verified: boolean) =>
		concat(new Uint8Array([CMD_ADD_CONTACT, verified ? FLAG_VERIFIED : 0]), pub, encodeName(name)),
	clearContacts: () => new Uint8Array([CMD_CLEAR_CONTACTS]),
	wipe: () => new Uint8Array([CMD_WIPE])
};

export interface DeviceInfo {
	pub: Uint8Array;
	version: number;
	contacts: number;
	blocks: number;
	name: string;
}

export function parseInfo(data: Uint8Array): DeviceInfo {
	if (data.length < KEY_SIZE + 4) throw new ProtocolError('short info');
	const [name, end] = decodeName(data, KEY_SIZE + 4);
	if (end !== data.length) throw new ProtocolError('trailing bytes');
	return {
		pub: data.slice(0, KEY_SIZE),
		version: data[KEY_SIZE],
		contacts: data[KEY_SIZE + 1],
		blocks: data[KEY_SIZE + 2] | (data[KEY_SIZE + 3] << 8),
		name
	};
}

export function parseProvisionReply(body: Uint8Array): { command: number; status: number; data: Uint8Array } {
	if (body.length < 2) throw new ProtocolError('short reply');
	return { command: body[0], status: body[1], data: body.subarray(2) };
}
