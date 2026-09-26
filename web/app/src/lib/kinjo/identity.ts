// The laptop node's own key, name and contact list, kept in this browser.

import { bytesToHex, hexToBytes, randomBytes } from '@noble/hashes/utils.js';

import type { Contact } from './mesh';

const KEY = 'kinjo.laptop.priv';
const NAME = 'kinjo.laptop.name';
const CONTACTS = 'kinjo.contacts';

function get(key: string): string | null {
	try {
		return localStorage.getItem(key);
	} catch {
		return null;
	}
}

function set(key: string, value: string | null): void {
	try {
		if (value === null) localStorage.removeItem(key);
		else localStorage.setItem(key, value);
	} catch {
		// private mode: the identity lives only for this tab
	}
}

/** Loads the laptop's X25519 private key. Makes one on first use. */
export function loadPrivateKey(): Uint8Array {
	const stored = get(KEY);
	if (stored && stored.length === 64) return hexToBytes(stored);
	const priv = randomBytes(32);
	set(KEY, bytesToHex(priv));
	return priv;
}

export function resetPrivateKey(): void {
	set(KEY, null);
}

export const loadName = () => get(NAME) ?? '';
export const saveName = (name: string) => set(NAME, name);

type StoredContact = { name: string; pub: string; verifiedHuman?: boolean; revoked?: boolean };

export function loadContacts(): Contact[] {
	try {
		const list = JSON.parse(get(CONTACTS) ?? '[]') as StoredContact[];
		return list.map((c) => ({ ...c, pub: hexToBytes(c.pub) }));
	} catch {
		return [];
	}
}

export function saveContacts(contacts: Contact[]): void {
	set(CONTACTS, JSON.stringify(contacts.map((c) => ({ ...c, pub: bytesToHex(c.pub) }))));
}
