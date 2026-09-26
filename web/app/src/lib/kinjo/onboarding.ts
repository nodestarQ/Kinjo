// KinjoOnboarding contract (contracts/src/KinjoOnboarding.sol): ABI, EIP-712 requests and the
// relayer request format. Shared by the web app and web/relayer, so keep it free of browser APIs.

import { parseAbi, type Address, type Hex } from 'viem';

export const onboardingAbi = parseAbi([
	'function join(address owner, string label, string deviceLabel, bytes32 deviceKey, uint256 deadline, bytes signature) payable',
	'function addDevice(address owner, string deviceLabel, bytes32 deviceKey, uint256 deadline, bytes signature) payable',
	'function revokeDevice(address owner, string deviceLabel, uint256 deadline, bytes signature) payable',
	'function release(string label)',
	'function setVerifiedHuman(address owner, bool verified)',
	'function nonces(address owner) view returns (uint256)',
	'function fee() view returns (uint256)',
	'function ownerOfLabel(bytes32 labelHash) view returns (address)',
	'function accountOf(address owner) view returns (address registry, address resolver, string label, bool verifiedHuman)',
	'error AlreadyJoined()',
	'error NotJoined()',
	'error LabelTaken()',
	'error InvalidLabel()',
	'error InvalidSignature()',
	'error Expired()',
	'error WrongFee()',
	'event DeviceAdded(address indexed owner, string deviceLabel, bytes32 deviceKey)',
	'event DeviceRevoked(address indexed owner, string deviceLabel)',
	// ENSv2 registry errors that surface through KinjoOnboarding
	'error LabelAlreadyRegistered(string label)',
	'error LabelAlreadyReserved(string label)',
	'error LabelExpired(uint256 tokenId)',
	'error EACUnauthorizedAccountRoles(uint256 resource, uint256 roleBitmap, address account)',
	'error ERC1155InvalidReceiver(address receiver)'
]);

export const eip712Types = {
	Join: [
		{ name: 'owner', type: 'address' },
		{ name: 'label', type: 'string' },
		{ name: 'deviceLabel', type: 'string' },
		{ name: 'deviceKey', type: 'bytes32' },
		{ name: 'nonce', type: 'uint256' },
		{ name: 'deadline', type: 'uint256' }
	],
	AddDevice: [
		{ name: 'owner', type: 'address' },
		{ name: 'deviceLabel', type: 'string' },
		{ name: 'deviceKey', type: 'bytes32' },
		{ name: 'nonce', type: 'uint256' },
		{ name: 'deadline', type: 'uint256' }
	],
	RevokeDevice: [
		{ name: 'owner', type: 'address' },
		{ name: 'deviceLabel', type: 'string' },
		{ name: 'nonce', type: 'uint256' },
		{ name: 'deadline', type: 'uint256' }
	]
} as const;

export function eip712Domain(chainId: number, verifyingContract: Address) {
	return { name: 'KinjoOnboarding', version: '1', chainId, verifyingContract } as const;
}

/** Same rule as the contract: 1 to 32 characters of a-z, 0-9 and "-". */
export const LABEL_PATTERN = /^[a-z0-9-]{1,32}$/;

export type RelayRequest =
	| { action: 'join'; owner: Address; label: string; deviceLabel: string; deviceKey: Hex; deadline: string; signature: Hex }
	| { action: 'addDevice'; owner: Address; deviceLabel: string; deviceKey: Hex; deadline: string; signature: Hex }
	| { action: 'revokeDevice'; owner: Address; deviceLabel: string; deadline: string; signature: Hex };

/** A request before the owner signed it. */
export type UnsignedRequest = RelayRequest extends infer R ? (R extends RelayRequest ? Omit<R, 'signature'> : never) : never;

/** The EIP-712 message the owner signs for a request (deadline in seconds). */
export function typedMessage(r: UnsignedRequest, nonce: bigint) {
	const deadline = BigInt(r.deadline);
	switch (r.action) {
		case 'join':
			return {
				primaryType: 'Join' as const,
				message: { owner: r.owner, label: r.label, deviceLabel: r.deviceLabel, deviceKey: r.deviceKey, nonce, deadline }
			};
		case 'addDevice':
			return {
				primaryType: 'AddDevice' as const,
				message: { owner: r.owner, deviceLabel: r.deviceLabel, deviceKey: r.deviceKey, nonce, deadline }
			};
		case 'revokeDevice':
			return { primaryType: 'RevokeDevice' as const, message: { owner: r.owner, deviceLabel: r.deviceLabel, nonce, deadline } };
	}
}

/** The contract call that carries a signed request. */
export function contractCall(r: RelayRequest) {
	const deadline = BigInt(r.deadline);
	switch (r.action) {
		case 'join':
			return { functionName: 'join' as const, args: [r.owner, r.label, r.deviceLabel, r.deviceKey, deadline, r.signature] as const };
		case 'addDevice':
			return { functionName: 'addDevice' as const, args: [r.owner, r.deviceLabel, r.deviceKey, deadline, r.signature] as const };
		case 'revokeDevice':
			return { functionName: 'revokeDevice' as const, args: [r.owner, r.deviceLabel, deadline, r.signature] as const };
	}
}

/** Checks the shape of a request from the network. Returns an error message or null. */
export function validateRequest(r: unknown): string | null {
	if (!r || typeof r !== 'object') return 'request must be an object';
	const q = r as Record<string, unknown>;
	const isHex = (v: unknown, bytes?: number) =>
		typeof v === 'string' && /^0x[0-9a-fA-F]*$/.test(v) && (bytes === undefined || v.length === 2 + bytes * 2);
	if (!['join', 'addDevice', 'revokeDevice'].includes(q.action as string)) return 'unknown action';
	if (!isHex(q.owner, 20)) return 'owner must be an address';
	if (!isHex(q.signature) || (q.signature as string).length < 4) return 'signature missing';
	if (typeof q.deadline !== 'string' || !/^\d+$/.test(q.deadline)) return 'deadline must be a decimal string';
	if (q.action === 'join' && (typeof q.label !== 'string' || !LABEL_PATTERN.test(q.label))) return 'invalid name';
	if (typeof q.deviceLabel !== 'string' || !LABEL_PATTERN.test(q.deviceLabel)) return 'invalid device name';
	if (q.action !== 'revokeDevice' && !isHex(q.deviceKey, 32)) return 'deviceKey must be 32 bytes';
	return null;
}
