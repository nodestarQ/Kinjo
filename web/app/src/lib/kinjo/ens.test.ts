// Runs against the local fork (contracts/script/local-fork.sh). Skipped if KINJO_ONBOARDING isn't set.

import { createTestClient, createWalletClient, http, parseEther, type Address } from 'viem';
import { generatePrivateKey, privateKeyToAccount } from 'viem/accounts';
import { describe, expect, it } from 'vitest';

import { createEns, keyHex, ownerLabel } from './ens';
import { onboardingAbi } from './onboarding';

const onboarding = process.env.KINJO_ONBOARDING as Address | undefined;
const rpcUrl = process.env.KINJO_RPC_URL ?? 'http://127.0.0.1:8545';
const team = process.env.KINJO_TEAM as Address | undefined;

describe('ownerLabel', () => {
	it('finds the owner label', () => {
		expect(ownerLabel('handheld.alice.kinjo.eth')).toBe('alice');
		expect(ownerLabel('alice.kinjo.eth')).toBe('alice');
		expect(ownerLabel('vitalik.eth')).toBeNull();
	});
});

describe.skipIf(!onboarding)('ENS on the local fork', () => {
	const ens = createEns({ chainId: 31337, rpcUrl, onboarding: onboarding!, relayerUrl: '' });
	const test = createTestClient({ chain: ens.chain, mode: 'anvil', transport: http(rpcUrl) });
	const owner = privateKeyToAccount(generatePrivateKey());
	const wallet = createWalletClient({ chain: ens.chain, transport: http(rpcUrl), account: owner });
	const label = `ens${Date.now() % 1_000_000}`;
	const device = `handheld.${label}.kinjo.eth`;
	const pub1 = new Uint8Array(32).fill(7);
	const pub2 = new Uint8Array(32).fill(9);

	it('joins (owner pays) and resolves the device as a contact', { timeout: 60_000 }, async () => {
		await test.setBalance({ address: owner.address, value: parseEther('1') });
		expect(await ens.labelFree(label)).toBe(true);
		await ens.submit(wallet, { action: 'join', owner: owner.address, label, deviceLabel: 'handheld', deviceKey: keyHex(pub1), deadline: ens.deadline() });

		expect(await ens.labelFree(label)).toBe(false);
		expect((await ens.account(owner.address))?.label).toBe(label);
		const contact = await ens.resolveContact(device);
		expect(contact.pub).toEqual(pub1);
		expect(contact.verifiedHuman).toBe(false);
	});

	it('sees the World ID badge once the team sets it', { timeout: 60_000 }, async () => {
		const teamWallet = createWalletClient({ chain: ens.chain, transport: http(rpcUrl), account: team! });
		const hash = await teamWallet.writeContract({
			address: onboarding!,
			abi: onboardingAbi,
			functionName: 'setVerifiedHuman',
			args: [owner.address, true]
		});
		await ens.client.waitForTransactionReceipt({ hash });
		expect(await ens.verifiedHuman(label)).toBe(true);
		expect((await ens.resolveContact(device)).verifiedHuman).toBe(true);
	});

	it('sync marks a revoked device and picks up a new key', { timeout: 60_000 }, async () => {
		let contacts = [await ens.resolveContact(device)];
		await ens.submit(wallet, { action: 'revokeDevice', owner: owner.address, deviceLabel: 'handheld', deadline: ens.deadline() });
		let result = await ens.sync(contacts);
		expect(result.contacts[0].revoked).toBe(true);
		expect(result.changes).toEqual([`${device} revoked`]);

		contacts = result.contacts;
		await ens.submit(wallet, { action: 'addDevice', owner: owner.address, deviceLabel: 'handheld', deviceKey: keyHex(pub2), deadline: ens.deadline() });
		result = await ens.sync(contacts);
		expect(result.contacts[0].revoked).toBe(false);
		expect(result.contacts[0].pub).toEqual(pub2);
		expect(result.changes).toEqual([`${device} authorized again`]);
	});

	it('signs requests the relayer format accepts', async () => {
		const signed = await ens.sign(wallet, { action: 'revokeDevice', owner: owner.address, deviceLabel: 'handheld', deadline: ens.deadline() });
		expect(signed.signature).toMatch(/^0x[0-9a-f]{130}$/);
	});
});
