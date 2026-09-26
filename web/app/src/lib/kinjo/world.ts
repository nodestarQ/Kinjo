// World ID badge, browser side (docs/onboarding.md#world-id-optional-badge).
// The relayer signs the request and checks the proof. This only runs IDKit and shows the QR code.

import { IDKit, IDKitErrorCodes, proofOfHuman, type IDKitRequest, type IDKitResult, type RpContext } from '@worldcoin/idkit-core';
import type { Address, Hex } from 'viem';

interface Context {
	app_id: `app_${string}`;
	action: string;
	environment: 'staging' | 'production';
	rp_context: RpContext;
}

/** Thrown with a message fit for the user. `retry` is false when trying again won't help. */
export class WorldError extends Error {
	constructor(
		message: string,
		readonly retry = true
	) {
		super(message);
	}
}

async function post<T>(relayerUrl: string, path: string, body: object): Promise<T> {
	if (!relayerUrl) throw new WorldError('World ID needs the relayer, which is not set up here', false);
	const res = await fetch(relayerUrl + path, {
		method: 'POST',
		headers: { 'content-type': 'application/json' },
		body: JSON.stringify(body)
	});
	const json = await res.json().catch(() => ({}));
	if (res.status === 503) throw new WorldError('World ID is not set up on this deployment yet', false);
	if (!res.ok) throw new WorldError(json.error ?? `relayer answered ${res.status}`);
	return json as T;
}

/** A started request: show `uri` as a QR code, then await `result`. */
export interface WorldRequest {
	uri: string;
	environment: string;
	result: Promise<IDKitResult>;
}

/**
 * Asks World App for a proof of human. The signal is the owner's address, so the relayer can
 * check the proof was made for this wallet and nobody can reuse it for another one.
 */
export async function requestProof(relayerUrl: string, owner: Address, abort: AbortSignal): Promise<WorldRequest> {
	const ctx = await post<Context>(relayerUrl, '/world/context', {});
	const request: IDKitRequest = await IDKit.request({
		app_id: ctx.app_id,
		action: ctx.action,
		rp_context: ctx.rp_context,
		allow_legacy_proofs: true,
		environment: ctx.environment
	}).preset(proofOfHuman({ signal: owner.toLowerCase() }));

	const result = request.pollUntilCompletion({ pollInterval: 2000, timeout: 5 * 60_000, signal: abort }).then((done) => {
		if (done.success) return done.result;
		throw new WorldError(MESSAGES[done.error] ?? `World ID failed: ${done.error}`);
	});
	return { uri: request.connectorURI, environment: ctx.environment, result };
}

const MESSAGES: Partial<Record<IDKitErrorCodes, string>> = {
	[IDKitErrorCodes.Cancelled]: 'Cancelled.',
	[IDKitErrorCodes.Timeout]: 'No answer from World App within 5 minutes.',
	[IDKitErrorCodes.UserRejected]: 'You declined in World App.',
	[IDKitErrorCodes.VerificationRejected]: 'You declined in World App.',
	[IDKitErrorCodes.CredentialUnavailable]: 'This World ID has no proof of human (Orb verification) yet.',
	[IDKitErrorCodes.InclusionProofPending]: 'Your World ID is still being registered. Try again in a few minutes.',
	[IDKitErrorCodes.MaxVerificationsReached]: 'This World ID was already used for this.',
	[IDKitErrorCodes.ConnectionFailed]: 'Could not reach World App. Check the internet connection.'
};

/** Sends the proof to the relayer, which checks it and registers label.verified.kinjo.eth. */
export function submitProof(relayerUrl: string, owner: Address, proof: IDKitResult) {
	return post<{ hash?: Hex; already?: true }>(relayerUrl, '/world/verify', { owner, proof });
}
