// Runs against the local fork (contracts/script/local-fork.sh). Skipped if KINJO_ONBOARDING isn't set.

import assert from 'node:assert/strict';
import { after, before, describe, it } from 'node:test';
import { createPublicClient, http, type Address, type Hex } from 'viem';
import { generatePrivateKey, privateKeyToAccount } from 'viem/accounts';
import { sepolia } from 'viem/chains';

import { eip712Domain, eip712Types, onboardingAbi, typedMessage, type RelayRequest, type UnsignedRequest } from '../app/src/lib/kinjo/onboarding.ts';
import { configFromEnv, startServer } from './server.ts';

const enabled = !!process.env.KINJO_ONBOARDING;

describe('relayer on the local fork', { skip: !enabled && 'start contracts/script/local-fork.sh and set KINJO_ONBOARDING' }, () => {
	const config = enabled ? { ...configFromEnv(), chainId: 31337 } : ({} as ReturnType<typeof configFromEnv>);
	const chain = { ...sepolia, id: 31337 };
	const client = createPublicClient({ chain, transport: http(config.rpcUrl) });
	const owner = privateKeyToAccount(generatePrivateKey());
	const label = `test${Date.now() % 1_000_000}`;
	const deviceKey = ('0x' + 'ab'.repeat(32)) as Hex;
	let server: Awaited<ReturnType<typeof startServer>>;
	let url = '';

	before(async () => {
		server = await startServer(config, 0);
		url = `http://127.0.0.1:${(server.address() as { port: number }).port}`;
	});
	after(() => server?.close());

	async function signed(r: UnsignedRequest, signer = owner): Promise<RelayRequest> {
		const nonce = await client.readContract({ address: config.onboarding, abi: onboardingAbi, functionName: 'nonces', args: [r.owner] });
		const signature = await signer.signTypedData({
			domain: eip712Domain(31337, config.onboarding),
			types: eip712Types,
			...typedMessage(r, nonce)
		});
		return { ...r, signature } as RelayRequest;
	}

	async function post(body: unknown): Promise<{ status: number; json: { hash?: Hex; error?: string } }> {
		const res = await fetch(url + '/relay', { method: 'POST', body: JSON.stringify(body) });
		return { status: res.status, json: await res.json() };
	}

	const deadline = () => String(Math.floor(Date.now() / 1000) + 600);
	const text = (name: string, key: string) => client.getEnsText({ name, key });

	it('joins a new owner and the device key resolves through ENS', async () => {
		const req = await signed({ action: 'join', owner: owner.address, label, deviceLabel: 'handheld', deviceKey, deadline: deadline() });
		const { status, json } = await post(req);
		assert.equal(status, 200, json.error);
		const receipt = await client.waitForTransactionReceipt({ hash: json.hash! });
		assert.equal(receipt.status, 'success');
		assert.equal(await text(`handheld.${label}.kinjo.eth`, 'xyz.kinjo.encryption-key'), deviceKey);
		assert.equal((await client.getEnsAddress({ name: `${label}.kinjo.eth` }))?.toLowerCase(), owner.address.toLowerCase());
	});

	it('revokes the device', async () => {
		const req = await signed({ action: 'revokeDevice', owner: owner.address, deviceLabel: 'handheld', deadline: deadline() });
		const { status, json } = await post(req);
		assert.equal(status, 200, json.error);
		await client.waitForTransactionReceipt({ hash: json.hash! });
		assert.ok(!(await text(`handheld.${label}.kinjo.eth`, 'xyz.kinjo.encryption-key'))); // viem returns null for an empty record
	});

	it('rejects a signature from someone else', async () => {
		const other = privateKeyToAccount(generatePrivateKey());
		const req = await signed({ action: 'addDevice', owner: owner.address, deviceLabel: 'laptop', deviceKey, deadline: deadline() }, other);
		const { status, json } = await post(req);
		assert.equal(status, 400);
		assert.equal(json.error, 'InvalidSignature');
	});

	it('rejects bad input before touching the chain', async () => {
		const { status, json } = await post({ action: 'join', owner: owner.address as Address, label: 'Bad Name', deviceLabel: 'x', deviceKey, deadline: deadline(), signature: '0x1234' });
		assert.equal(status, 400);
		assert.equal(json.error, 'invalid name');
	});
});
