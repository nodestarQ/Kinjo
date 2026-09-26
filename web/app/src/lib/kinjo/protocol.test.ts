import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { hexToBytes, bytesToHex } from '@noble/hashes/utils.js';
import { describe, expect, it } from 'vitest';

import * as k from './protocol';

const VECTORS = fileURLToPath(new URL('../../../../../protocol/test-vectors/', import.meta.url));
const load = (name: string) => JSON.parse(readFileSync(VECTORS + name, 'utf8'));
const hex = (s: string) => hexToBytes(s);

describe('header', () => {
	it('matches the vectors', () => {
		for (const c of load('header.json').cases) {
			const f = c.fields;
			const h: k.Header = {
				version: f.version, type: f.type, flags: f.flags, ttl: f.ttl, messageId: f.message_id,
				source: f.source, destination: f.destination, fragIndex: f.frag_index, fragCount: f.frag_count
			};
			expect(bytesToHex(k.encodeHeader(h))).toBe(c.header);
			expect(bytesToHex(k.headerAd(h))).toBe(c.ad);
			expect(k.decodeHeader(hex(c.header))).toEqual(h);
		}
	});
});

describe('fragments', () => {
	it('match the vectors and reassemble in any order', () => {
		for (const c of load('fragment.json').cases) {
			const packets = k.fragment(c.type, c.message_id, c.source, c.destination, hex(c.body), c.ttl);
			expect(packets.map(bytesToHex)).toEqual(c.packets);
			const r = new k.Reassembler();
			let done = null;
			for (const p of [...packets].reverse()) done = r.push(p, 0) ?? done;
			expect(bytesToHex(done!.body)).toBe(c.body);
		}
	});
});

describe('crypto', () => {
	const keys = load('keys.json');
	it('derives the shared key on both sides', () => {
		const hp = hex(keys.handheld.private);
		const lp = hex(keys.laptop.private);
		expect(bytesToHex(k.publicKey(hp))).toBe(keys.handheld.public);
		expect(k.nodeId(hex(keys.handheld.public))).toBe(keys.handheld.node_id);
		expect(bytesToHex(k.deriveKey(hp, hex(keys.laptop.public)))).toBe(keys.key);
		expect(bytesToHex(k.deriveKey(lp, hex(keys.handheld.public)))).toBe(keys.key);
	});

	it('seals and opens the vectors', () => {
		for (const c of load('sealed.json').cases) {
			const packets = k.seal(hex(c.key), c.source, c.destination, c.message_id, hex(c.plaintext), hex(c.nonce), c.ttl);
			expect(packets.map(bytesToHex)).toEqual(c.packets);
			const r = new k.Reassembler();
			let done = null;
			for (const p of c.packets) done = r.push(hex(p), 0) ?? done;
			expect(bytesToHex(k.openSealed(hex(c.key), done!.header, done!.body))).toBe(c.plaintext);
		}
	});

	it('rejects a tampered header', () => {
		const c = load('sealed.json').cases[0];
		const bad = hex(c.packets[0]);
		bad[9] ^= 1; // source
		const { header, fragment } = k.parsePacket(bad);
		expect(() => k.openSealed(hex(c.key), header, fragment)).toThrow();
	});
});

describe('payloads', () => {
	it('decodes drawings with colors', () => {
		const v = load('drawing.json');
		expect(k.PALETTE).toEqual(v.palette);
		for (const c of v.cases) {
			expect(k.decodeDrawing(hex(c.plaintext))).toEqual(c.decoded);
			expect(k.decodeDrawingColored(hex(c.plaintext)).map((s) => s.color)).toEqual(c.decoded_colors);
		}
		expect(() => k.decodeDrawingColored(new Uint8Array([k.KIND_DRAWING, 0, 8]))).toThrow();
	});

	it('encodes and decodes notes like the reference', () => {
		for (const c of load('drawing.json').notes) {
			const strokes = c.strokes.map((points: [number, number][], i: number) => ({ color: c.colors[i], points }));
			expect(bytesToHex(k.notePlaintext(c.text, strokes))).toBe(c.plaintext);
			const note = k.decodeNote(hex(c.plaintext));
			expect(note.text).toBe(c.text);
			expect(note.strokes.map((s) => s.points)).toEqual(c.decoded);
		}
		expect(() => k.decodeNote(new Uint8Array([k.KIND_NOTE, 5, 97]))).toThrow();
	});

	it('encodes drawings like the reference', () => {
		for (const c of load('drawing.json').cases) {
			const strokes = c.strokes.map((points: [number, number][], i: number) => ({ color: c.colors[i], points }));
			expect(bytesToHex(k.encodeDrawing(strokes))).toBe(c.plaintext);
		}
		expect(() => k.encodeDrawing([{ color: 0, points: [[320, 0]] }])).toThrow();
	});
});

describe('serial', () => {
	it('COBS matches the vectors', () => {
		const v = load('cobs.json');
		for (const c of v.cases) {
			expect(bytesToHex(k.cobsEncode(hex(c.raw)))).toBe(c.encoded);
			expect(bytesToHex(k.cobsDecode(hex(c.encoded)))).toBe(c.raw);
		}
		for (const c of v.decode_only) expect(bytesToHex(k.cobsDecode(hex(c.encoded)))).toBe(c.raw);
		expect(bytesToHex(k.serialFrame(v.serial_frame.frame_type, hex(v.serial_frame.body)))).toBe(v.serial_frame.wire);
	});

	it('frame reader skips boot text', () => {
		const f = load('cobs.json').serial_frame;
		const stream = new Uint8Array([...new TextEncoder().encode('ESP-ROM boot\r\n'), 0, ...hex(f.wire)]);
		const r = new k.FrameReader();
		const frames = [...r.push(stream.subarray(0, 10)), ...r.push(stream.subarray(10))];
		expect(frames.map((x) => [x.type, bytesToHex(x.body)])).toEqual([[f.frame_type, f.body]]);
	});
});

describe('provisioning', () => {
	const v = load('provision.json');
	const req = (label: string) => v.requests.find((r: { label: string }) => r.label === label);

	it('builds the request vectors', () => {
		const lp = hex(req('add_contact_verified').args.pub);
		expect(bytesToHex(k.provision.info())).toBe(req('info').body);
		expect(bytesToHex(k.provision.setName(req('set_name').args.name))).toBe(req('set_name').body);
		expect(bytesToHex(k.provision.setName(''))).toBe(req('clear_name').body);
		expect(bytesToHex(k.provision.addContact(req('add_contact_verified').args.name, lp, true))).toBe(
			req('add_contact_verified').body
		);
		expect(bytesToHex(k.provision.clearContacts())).toBe(req('clear_contacts').body);
		expect(bytesToHex(k.provision.wipe())).toBe(req('wipe').body);
	});

	it('parses replies and info', () => {
		for (const r of v.replies) {
			const got = k.parseProvisionReply(hex(r.body));
			expect([got.command, got.status, bytesToHex(got.data)]).toEqual([r.command, r.status, r.data]);
		}
		for (const i of v.infos) {
			const info = k.parseInfo(hex(i.data));
			expect([bytesToHex(info.pub), info.version, info.contacts, info.blocks, info.name]).toEqual([
				i.pub, i.version, i.contacts, i.blocks, i.name
			]);
		}
	});
});
