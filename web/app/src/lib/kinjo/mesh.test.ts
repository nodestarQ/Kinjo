import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { hexToBytes } from '@noble/hashes/utils.js';
import { describe, expect, it } from 'vitest';

import { MeshNode, contactUpdates, ownDevices, type Contact, type Message } from './mesh';
import { Reassembler, contactUpdate, decodeDrawingColored, deriveKey, openSealed } from './protocol';

const VECTORS = fileURLToPath(new URL('../../../../../protocol/test-vectors/', import.meta.url));
const load = (name: string) => JSON.parse(readFileSync(VECTORS + name, 'utf8'));
const keys = load('keys.json');
const sealed = load('sealed.json').cases;

// The vectors are sealed from the handheld test key to the laptop test key.
const handheld: Contact = { name: 'handheld.alice.kinjo.eth', pub: hexToBytes(keys.handheld.public) };
const radioRx = (packetHex: string) => new Uint8Array([1, 2, 3, 4, 5, 6, 0, ...hexToBytes(packetHex)]);

function laptop(contacts: Contact[]) {
	const messages: Message[] = [];
	const node = new MeshNode(hexToBytes(keys.laptop.private), (m) => messages.push(m));
	node.contacts = contacts;
	return { node, messages };
}

describe('MeshNode', () => {
	it('opens text and drawings from a known contact', () => {
		const { node, messages } = laptop([handheld]);
		for (const c of sealed) for (const p of c.packets) node.handleRadioRx(radioRx(p));
		expect(messages.map((m) => m.status)).toEqual(['verified', 'verified']);
		expect(messages[0].text).toBe('gm Tokyo');
		expect(messages[1].strokes?.length).toBeGreaterThan(0);
	});

	it('ignores duplicates', () => {
		const { node, messages } = laptop([handheld]);
		node.handleRadioRx(radioRx(sealed[0].packets[0]));
		node.handleRadioRx(radioRx(sealed[0].packets[0]));
		expect(messages.length).toBe(1);
	});

	it('rejects a revoked contact and flags unknown senders', () => {
		const revoked = laptop([{ ...handheld, revoked: true }]);
		revoked.node.handleRadioRx(radioRx(sealed[0].packets[0]));
		expect(revoked.messages[0].status).toBe('revoked');
		expect(revoked.messages[0].text).toBeUndefined();

		const unknown = laptop([]);
		unknown.node.handleRadioRx(radioRx(sealed[0].packets[0]));
		expect(unknown.messages[0].status).toBe('unknown sender');
	});

	it('flags a contact whose ENS key does not match', () => {
		const wrongKey = { ...handheld, pub: hexToBytes(keys.laptop.public) };
		wrongKey.pub = new Uint8Array(wrongKey.pub);
		wrongKey.pub.set(hexToBytes(keys.handheld.public).subarray(0, 4)); // same node id, other key
		const { node, messages } = laptop([wrongKey]);
		node.handleRadioRx(radioRx(sealed[0].packets[0]));
		expect(messages[0].status).toBe('failed to open');
	});

	it('seals drawings the handheld can open', () => {
		const { node } = laptop([handheld]);
		const strokes = [{ color: 5, points: Array.from({ length: 300 }, (_, i): [number, number] => [i % 320, (i * 3) % 240]) }];
		const packets = node.sealDrawing(handheld, strokes);
		expect(packets.length).toBeGreaterThan(1); // big enough for several fragments
		const r = new Reassembler();
		let done = null;
		for (const p of packets) done = r.push(p, 0) ?? done;
		const key = deriveKey(hexToBytes(keys.handheld.private), node.pub);
		const pt = openSealed(key, done!.header, done!.body);
		expect(decodeDrawingColored(pt)[0].color).toBe(5);
	});

	it('seals texts the handheld can open', () => {
		const { node } = laptop([handheld]);
		const packets = node.sealText(handheld, 'hello handheld');
		const r = new Reassembler();
		let done = null;
		for (const p of packets) done = r.push(p, 0) ?? done;
		const key = deriveKey(hexToBytes(keys.handheld.private), node.pub);
		expect(new TextDecoder().decode(openSealed(key, done!.header, done!.body).subarray(1))).toBe('hello handheld');
	});
});

describe('contact updates', () => {
	const bob: Contact = { name: 'handheld.bob.kinjo.eth', pub: new Uint8Array(32).fill(7) };
	const carol: Contact = { name: 'laptop.carol.kinjo.eth', pub: new Uint8Array(32).fill(9), verifiedHuman: true };

	it('go only to the laptop\'s own devices', () => {
		const revokedOwn = { ...handheld, name: 'phone.alice.kinjo.eth', revoked: true };
		expect(ownDevices('laptop.alice.kinjo.eth', [handheld, bob, revokedOwn]).map((c) => c.name)).toEqual([handheld.name]);
		expect(ownDevices('', [handheld])).toEqual([]);
	});

	it('cover revocations, new keys, badges and nothing else', () => {
		const before = [bob, carol, handheld];
		const after = [{ ...bob, revoked: true }, { ...carol, verifiedHuman: false }, handheld];
		const got = contactUpdates(before, after).map((u) => [u.name, u.plaintext]);
		expect(got).toEqual([
			[bob.name, contactUpdate.revoke(bob.name)],
			[carol.name, contactUpdate.set(carol.name, carol.pub, false)]
		]);
		const back = contactUpdates(after, [bob, { ...carol, pub: new Uint8Array(32).fill(1), verifiedHuman: false }, handheld]);
		expect(back.map((u) => u.name)).toEqual([bob.name, carol.name]);
	});

	it('are sealed to the device and open with its key', () => {
		const { node, messages } = laptop([handheld]);
		const pt = contactUpdate.revoke(bob.name);
		const packets = node.sealUpdate(handheld, pt);
		expect(packets.length).toBe(1);
		const done = new Reassembler().push(packets[0], 0)!;
		expect(openSealed(deriveKey(hexToBytes(keys.handheld.private), hexToBytes(keys.laptop.public)), done.header, done.body)).toEqual(pt);
		expect(messages).toEqual([]); // not a chat message
	});
});
