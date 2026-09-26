// World ID badge checks with a fake Developer Portal and chain. No network needed.

import assert from 'node:assert/strict';
import { mkdtemp } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { beforeEach, describe, it } from 'node:test';
import { generatePrivateKey, privateKeyToAccount } from 'viem/accounts';
import type { Address, Hex } from 'viem';

import { hashSignal as idkitHashSignal } from '../app/node_modules/@worldcoin/idkit-core/dist/hashing.js';
import { createWorld, hashSignal, signalFor, type WorldConfig } from './world.ts';

const alice = privateKeyToAccount(generatePrivateKey()).address;
const bob = privateKeyToAccount(generatePrivateKey()).address;

describe('World ID badge', () => {
	let config: WorldConfig;
	let verified: Address[];
	let portalOk: boolean;
	let joined: Set<Address>;

	beforeEach(async () => {
		config = {
			appId: 'app_staging_test',
			rpId: 'rp_test',
			signingKey: generatePrivateKey(),
			action: 'verify-human',
			environment: 'staging',
			verifyUrl: 'https://portal.test/api/v4/verify',
			stagingToken: 'staging-token',
			nullifierFile: join(await mkdtemp(join(tmpdir(), 'kinjo-world-')), 'nullifiers.json')
		};
		verified = [];
		portalOk = true;
		joined = new Set([alice, bob]);
	});

	const world = () =>
		createWorld(config, {
			account: async (owner) => (joined.has(owner) ? { label: 'x', verifiedHuman: verified.includes(owner) } : null),
			setVerified: async (owner) => {
				verified.push(owner);
				return '0xabc' as Hex;
			},
			fetch: (async (url: string, init: RequestInit) => {
				assert.equal(url, 'https://portal.test/api/v4/verify/rp_test');
				assert.equal((init.headers as Record<string, string>)['x-staging-verification-token'], 'staging-token');
				return new Response(JSON.stringify(portalOk ? { success: true } : { success: false, code: 'all_verifications_failed', detail: 'bad proof' }), {
					status: portalOk ? 200 : 400
				});
			}) as typeof fetch
		});

	const proof = (owner: Address, nullifier = '0x0123') => ({
		protocol_version: '4.0',
		action: 'verify-human',
		environment: 'staging',
		responses: [{ identifier: 'proof_of_human', nullifier, signal_hash: hashSignal(signalFor(owner)) }]
	});

	it('hashes the signal like IDKit', () => {
		assert.equal(hashSignal(signalFor(alice)), idkitHashSignal(signalFor(alice)));
	});

	it('signs a request context for the app', () => {
		const c = world().context();
		assert.equal(c.app_id, 'app_staging_test');
		assert.equal(c.rp_context.rp_id, 'rp_test');
		assert.match(c.rp_context.signature, /^0x[0-9a-f]{130}$/);
	});

	it('verifies a proof and registers the badge', async () => {
		assert.deepEqual(await world().verify(alice, proof(alice)), { hash: '0xabc' });
		assert.deepEqual(verified, [alice]);
		assert.deepEqual(await world().verify(alice, proof(alice)), { already: true });
	});

	it('rejects a proof World ID does not accept', async () => {
		portalOk = false;
		await assert.rejects(world().verify(alice, proof(alice)), /rejected the proof: bad proof/);
		assert.deepEqual(verified, []);
	});

	it('rejects a proof made for another wallet', async () => {
		await assert.rejects(world().verify(bob, proof(alice)), /another wallet/);
	});

	it('allows one Kinjo name per human', async () => {
		await world().verify(alice, proof(alice));
		await assert.rejects(world().verify(bob, proof(bob)), /already verified another Kinjo name/);
		assert.deepEqual(verified, [alice]);
	});

	it('needs a Kinjo name and the right action and environment', async () => {
		joined.delete(alice);
		await assert.rejects(world().verify(alice, proof(alice)), /claim a Kinjo name first/);
		joined.add(alice);
		await assert.rejects(world().verify(alice, { ...proof(alice), action: 'other' }), /another action/);
		await assert.rejects(world().verify(alice, { ...proof(alice), environment: 'production' }), /expected staging/);
	});
});
