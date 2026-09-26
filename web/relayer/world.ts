// World ID for the "verified human" badge (docs/onboarding.md#world-id-optional-badge).
// POST /world/context signs a proof request for the web app (the signing key stays here).
// POST /world/verify checks the proof with the World Developer Portal, allows one Kinjo name per
// human and then calls setVerifiedHuman, which registers label.verified.kinjo.eth.

import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { dirname } from 'node:path';
import { signRequest } from '@worldcoin/idkit-server';
import { hexToBytes, isAddress, keccak256, type Address, type Hex } from 'viem';

export interface WorldConfig {
	appId: string;
	rpId: string;
	signingKey: string;
	action: string;
	environment: 'staging' | 'production';
	verifyUrl: string;
	/** Staging only: token from opening a staging verification window in the Developer Portal. */
	stagingToken: string;
	/** Nullifier -> owner, so one human can only verify one name. */
	nullifierFile: string;
}

/** Null if World ID isn't set up on this deployment (the endpoints then answer 503). */
export function worldConfigFromEnv(env = process.env): WorldConfig | null {
	const { WORLD_APP_ID: appId, WORLD_RP_ID: rpId, WORLD_RP_SIGNING_KEY: signingKey } = env;
	if (!appId || !rpId || !signingKey) return null;
	const environment = env.WORLD_ENVIRONMENT === 'production' ? 'production' : 'staging';
	return {
		appId,
		rpId,
		signingKey,
		action: env.WORLD_ACTION ?? 'verify-human',
		environment,
		verifyUrl: env.WORLD_VERIFY_URL ?? 'https://developer.world.org/api/v4/verify',
		stagingToken: env.WORLD_STAGING_TOKEN ?? '',
		nullifierFile: env.WORLD_NULLIFIER_FILE ?? 'data/world-nullifiers.json'
	};
}

/** Same as hashSignal() in @worldcoin/idkit-core: keccak256 of the bytes, shifted into the field. */
export function hashSignal(signal: Hex): Hex {
	const hash = BigInt(keccak256(hexToBytes(signal))) >> 8n;
	return `0x${hash.toString(16).padStart(64, '0')}`;
}

/** The proof's signal is the owner's address, so a proof can't be used for anyone else. */
export const signalFor = (owner: Address): Hex => owner.toLowerCase() as Hex;

interface ProofResponse {
	protocol_version?: string;
	action?: string;
	environment?: string;
	responses?: { identifier?: string; nullifier?: string; signal_hash?: string }[];
}

export interface WorldDeps {
	/** The owner's account, or null if they have no Kinjo name. */
	account(owner: Address): Promise<{ label: string; verifiedHuman: boolean } | null>;
	/** Calls setVerifiedHuman(owner, true) from the team wallet. Returns the tx hash. */
	setVerified(owner: Address): Promise<Hex>;
	fetch?: typeof fetch;
}

export function createWorld(config: WorldConfig, deps: WorldDeps) {
	const doFetch = deps.fetch ?? fetch;

	async function loadNullifiers(): Promise<Record<string, string>> {
		try {
			return JSON.parse(await readFile(config.nullifierFile, 'utf8'));
		} catch {
			return {};
		}
	}

	async function saveNullifiers(map: Record<string, string>) {
		await mkdir(dirname(config.nullifierFile), { recursive: true });
		await writeFile(config.nullifierFile, JSON.stringify(map, null, '\t'));
	}

	/** What the web app needs to start IDKit. */
	function context() {
		const { sig, nonce, createdAt, expiresAt } = signRequest({ signingKeyHex: config.signingKey, action: config.action });
		return {
			app_id: config.appId,
			action: config.action,
			environment: config.environment,
			rp_context: { rp_id: config.rpId, nonce, created_at: createdAt, expires_at: expiresAt, signature: sig }
		};
	}

	/** Checks the proof and registers the badge. Throws an Error with a message fit for the user. */
	async function verify(owner: string, proof: ProofResponse): Promise<{ hash?: Hex; already?: true }> {
		if (!isAddress(owner)) throw new Error('bad owner address');
		const account = await deps.account(owner);
		if (!account) throw new Error('claim a Kinjo name first');
		if (account.verifiedHuman) return { already: true };

		if (proof?.action !== config.action) throw new Error('proof is for another action');
		if (proof.environment !== config.environment) throw new Error(`proof is from ${proof.environment}, expected ${config.environment}`);
		const item = proof.responses?.[0];
		if (!item?.nullifier) throw new Error('proof has no nullifier');
		if (item.signal_hash?.toLowerCase() !== hashSignal(signalFor(owner))) {
			throw new Error('proof was made for another wallet');
		}

		const res = await doFetch(`${config.verifyUrl}/${config.rpId}`, {
			method: 'POST',
			headers: {
				'content-type': 'application/json',
				...(config.environment === 'staging' && config.stagingToken ? { 'x-staging-verification-token': config.stagingToken } : {})
			},
			body: JSON.stringify(proof)
		});
		const result = (await res.json().catch(() => ({}))) as { success?: boolean; code?: string; detail?: string };
		if (!res.ok || !result.success) {
			throw new Error(`World ID rejected the proof: ${result.detail ?? result.code ?? res.status}`);
		}

		// One human, one name: the nullifier is the same every time this person verifies this action.
		const nullifier = BigInt(item.nullifier).toString();
		// A prefix is enough to tell World IDs apart in the log
		console.log(`world nullifier ${nullifier.slice(0, 12)}... for ${owner}`);
		const used = await loadNullifiers();
		const holder = used[nullifier];
		if (holder && holder !== owner.toLowerCase()) {
			throw new Error('this World ID already verified another Kinjo name');
		}
		const hash = await deps.setVerified(owner);
		used[nullifier] = owner.toLowerCase();
		await saveNullifiers(used);
		return { hash };
	}

	return { context, verify };
}
