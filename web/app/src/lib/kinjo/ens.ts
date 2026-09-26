// ENS side of Kinjo. Reads device keys and badges through the ENS Universal Resolver. Signs and
// submits KinjoOnboarding requests, through the relayer or paid by the owner directly.

import {
	createPublicClient,
	createWalletClient,
	custom,
	http,
	keccak256,
	toHex,
	type Address,
	type Chain,
	type EIP1193Provider,
	type Hex,
	type WalletClient
} from 'viem';
import { sepolia } from 'viem/chains';
import { bytesToHex, hexToBytes } from '@noble/hashes/utils.js';

import type { Contact } from './mesh';
import {
	contractCall,
	eip712Domain,
	eip712Types,
	onboardingAbi,
	typedMessage,
	type RelayRequest,
	type UnsignedRequest
} from './onboarding';

export const KEY_ENCRYPTION = 'xyz.kinjo.encryption-key';
export const KEY_VERIFIED = 'xyz.kinjo.verified-human';
export const PARENT = 'kinjo.eth';

export interface EnsConfig {
	chainId: number;
	rpcUrl: string;
	onboarding: Address;
	/** Empty: owners pay their own gas. */
	relayerUrl: string;
	/** Block the contract was deployed in: device events are read from here on. */
	deployBlock?: bigint;
}

/** `handheld.alice.kinjo.eth` → `alice`. Null if the name isn't under kinjo.eth. */
export function ownerLabel(name: string): string | null {
	const parts = name.split('.');
	if (parts.length < 3 || parts.slice(-2).join('.') !== PARENT) return null;
	return parts[parts.length - 3];
}

export function createEns(config: EnsConfig) {
	const chain: Chain = {
		...sepolia,
		id: config.chainId,
		rpcUrls: { default: { http: [config.rpcUrl] } }
	};
	const client = createPublicClient({ chain, transport: http(config.rpcUrl) });

	const text = async (name: string, key: string) => (await client.getEnsText({ name, key })) || null;

	/** The device's X25519 key from ENS. Null if it has none (revoked or never set). */
	async function deviceKey(name: string): Promise<Uint8Array | null> {
		const value = await text(name, KEY_ENCRYPTION);
		if (!value || !/^0x[0-9a-fA-F]{64}$/.test(value)) return null;
		return hexToBytes(value.slice(2));
	}

	/** True if `alice.verified.kinjo.eth` says "world" and points at the same address as `alice.kinjo.eth`. */
	async function verifiedHuman(label: string): Promise<boolean> {
		const badge = `${label}.verified.${PARENT}`;
		if ((await text(badge, KEY_VERIFIED)) !== 'world') return false;
		const [badgeAddr, ownerAddr] = await Promise.all([
			client.getEnsAddress({ name: badge }),
			client.getEnsAddress({ name: `${label}.${PARENT}` })
		]);
		return !!badgeAddr && badgeAddr.toLowerCase() === ownerAddr?.toLowerCase();
	}

	/** Builds a contact from ENS. Throws if the name has no key. */
	async function resolveContact(name: string): Promise<Contact> {
		const pub = await deviceKey(name);
		if (!pub) throw new Error(`${name} has no Kinjo key in ENS`);
		const label = ownerLabel(name);
		return { name, pub, verifiedHuman: label ? await verifiedHuman(label) : false };
	}

	/**
	 * Re-reads every contact. A cleared key marks it revoked. A new key replaces the old one
	 * (a wiped and re-registered device). Returns the updated list and what changed.
	 */
	async function sync(contacts: Contact[]): Promise<{ contacts: Contact[]; changes: string[] }> {
		const changes: string[] = [];
		const updated = await Promise.all(
			contacts.map(async (c) => {
				const pub = await deviceKey(c.name);
				const label = ownerLabel(c.name);
				const human = label ? await verifiedHuman(label) : false;
				if (!pub) {
					if (!c.revoked) changes.push(`${c.name} revoked`);
					return { ...c, revoked: true, verifiedHuman: human };
				}
				if (c.revoked) changes.push(`${c.name} authorized again`);
				else if (bytesToHex(pub) !== bytesToHex(c.pub)) changes.push(`${c.name} has a new key`);
				return { ...c, pub, revoked: false, verifiedHuman: human };
			})
		);
		return { contacts: updated, changes };
	}

	async function account(owner: Address) {
		const [registry, resolver, label, human] = await client.readContract({
			address: config.onboarding,
			abi: onboardingAbi,
			functionName: 'accountOf',
			args: [owner]
		});
		return registry === '0x0000000000000000000000000000000000000000' ? null : { registry, resolver, label, verifiedHuman: human };
	}

	/** The owner's current device labels, from the contract's DeviceAdded and DeviceRevoked events. */
	async function devicesOf(owner: Address): Promise<string[]> {
		const common = { address: config.onboarding, args: { owner }, fromBlock: config.deployBlock ?? 0n, toBlock: 'latest' } as const;
		const [added, revoked] = await Promise.all([
			client.getContractEvents({ ...common, abi: onboardingAbi, eventName: 'DeviceAdded' }),
			client.getContractEvents({ ...common, abi: onboardingAbi, eventName: 'DeviceRevoked' })
		]);
		const events = [...added.map((e) => ({ e, on: true })), ...revoked.map((e) => ({ e, on: false }))].sort(
			(a, b) => Number(a.e.blockNumber - b.e.blockNumber) || a.e.logIndex - b.e.logIndex
		);
		const active = new Set<string>();
		for (const { e, on } of events) {
			const label = (e.args as { deviceLabel: string }).deviceLabel;
			if (on) active.add(label);
			else active.delete(label);
		}
		return [...active];
	}

	async function labelFree(label: string): Promise<boolean> {
		if (label === 'verified') return false;
		const owner = await client.readContract({
			address: config.onboarding,
			abi: onboardingAbi,
			functionName: 'ownerOfLabel',
			args: [keccak256(toHex(label))]
		});
		return owner === '0x0000000000000000000000000000000000000000';
	}

	/** Owner signs the request with their wallet (EIP-712, no gas). */
	async function sign(wallet: WalletClient, request: UnsignedRequest): Promise<RelayRequest> {
		const nonce = await client.readContract({
			address: config.onboarding,
			abi: onboardingAbi,
			functionName: 'nonces',
			args: [request.owner]
		});
		const signature = await wallet.signTypedData({
			account: wallet.account ?? request.owner,
			domain: eip712Domain(config.chainId, config.onboarding),
			types: eip712Types,
			...typedMessage(request, nonce)
		});
		return { ...request, signature } as RelayRequest;
	}

	/** Sends a signed request through the relayer. Returns the tx hash. */
	async function relay(request: RelayRequest): Promise<Hex> {
		const res = await fetch(`${config.relayerUrl}/relay`, {
			method: 'POST',
			headers: { 'content-type': 'application/json' },
			body: JSON.stringify(request)
		});
		const json = (await res.json()) as { hash?: Hex; error?: string };
		if (!res.ok || !json.hash) throw new Error(json.error ?? `relayer answered ${res.status}`);
		return json.hash;
	}

	/** Owner pays: calls the contract directly with an empty signature. */
	async function direct(wallet: WalletClient, request: UnsignedRequest): Promise<Hex> {
		const fee = await client.readContract({ address: config.onboarding, abi: onboardingAbi, functionName: 'fee' });
		const { functionName, args } = contractCall({ ...request, signature: '0x' } as RelayRequest);
		return wallet.writeContract({
			account: wallet.account ?? request.owner,
			chain,
			address: config.onboarding,
			abi: onboardingAbi,
			functionName,
			args: args as never,
			value: fee
		});
	}

	/** Signs and relays if a relayer is set, else the owner pays. Waits for the receipt. */
	async function submit(wallet: WalletClient, request: UnsignedRequest): Promise<Hex> {
		await ensureChain(wallet);
		const hash = config.relayerUrl ? await relay(await sign(wallet, request)) : await direct(wallet, request);
		const receipt = await client.waitForTransactionReceipt({ hash });
		if (receipt.status !== 'success') throw new Error('transaction failed');
		return hash;
	}

	const provider = () => (globalThis as { ethereum?: EIP1193Provider }).ethereum;

	async function ensureChain(wallet: WalletClient) {
		if ((await wallet.getChainId()) === chain.id) return;
		try {
			await wallet.switchChain({ id: chain.id });
		} catch {
			await wallet.addChain({ chain });
			await wallet.switchChain({ id: chain.id });
		}
	}

	/** Connects the browser wallet (MetaMask etc.) and switches it to our chain. */
	async function connectWallet(): Promise<{ wallet: WalletClient; address: Address }> {
		const p = provider();
		if (!p) throw new Error('no browser wallet found');
		const wallet = createWalletClient({ chain, transport: custom(p) });
		const [address] = await wallet.requestAddresses();
		await ensureChain(wallet);
		return { wallet, address };
	}

	/** The account the wallet already allowed this site, without a popup. Null if none. */
	async function restoreWallet(): Promise<{ wallet: WalletClient; address: Address } | null> {
		const p = provider();
		if (!p) return null;
		const wallet = createWalletClient({ chain, transport: custom(p) });
		const [address] = await wallet.getAddresses();
		return address ? { wallet, address } : null;
	}

	const deadline = () => String(Math.floor(Date.now() / 1000) + 600);

	return { client, chain, deviceKey, verifiedHuman, resolveContact, sync, account, labelFree, sign, relay, direct, submit, connectWallet, restoreWallet, devicesOf, deadline };
}

export type Ens = ReturnType<typeof createEns>;

export const keyHex = (pub: Uint8Array) => ('0x' + bytesToHex(pub)) as Hex;
