// Relayer: submits owner-signed KinjoOnboarding requests and pays the gas (docs/onboarding.md).
// POST /relay checks the request, simulates the call and sends it from the team wallet.
// POST /world/context and /world/verify: World ID badge (world.ts), if WORLD_* is set.

import { readFile } from 'node:fs/promises';
import { createServer, type IncomingMessage, type ServerResponse } from 'node:http';
import { extname, join, normalize } from 'node:path';
import { fileURLToPath } from 'node:url';
import {
	BaseError,
	ContractFunctionRevertedError,
	createPublicClient,
	createWalletClient,
	http,
	type Address,
	type Chain,
	type Hex
} from 'viem';
import { privateKeyToAccount } from 'viem/accounts';
import { sepolia } from 'viem/chains';

import { contractCall, onboardingAbi, validateRequest, type RelayRequest } from '../app/src/lib/kinjo/onboarding.ts';
import { createWorld, worldConfigFromEnv, type WorldConfig } from './world.ts';

export interface Config {
	rpcUrl: string;
	onboarding: Address;
	/** Production: the team wallet key. Local fork: leave empty and set `team` (anvil impersonates it). */
	privateKey?: Hex;
	team?: Address;
	chainId: number;
	/** Comma-separated origins allowed to call /relay from a browser, or "*". */
	allowedOrigin: string;
	/** Requests per owner per window. */
	rateLimit: number;
	rateWindowMs: number;
	/** Longest accepted deadline, so signatures can't be hoarded. */
	maxDeadlineSeconds: number;
	/** Built web app to serve on all other paths (the Docker image sets it). Empty: API only. */
	staticDir: string;
	/** World ID badge. Null: not set up, the /world endpoints answer 503. */
	world: WorldConfig | null;
}

export function configFromEnv(env = process.env): Config {
	const onboarding = env.KINJO_ONBOARDING as Address | undefined;
	if (!onboarding) throw new Error('KINJO_ONBOARDING is not set');
	return {
		rpcUrl: env.KINJO_RPC_URL ?? 'https://ethereum-sepolia-rpc.publicnode.com',
		onboarding,
		privateKey: env.RELAYER_PRIVATE_KEY as Hex | undefined,
		team: env.KINJO_TEAM as Address | undefined,
		chainId: Number(env.KINJO_CHAIN_ID ?? sepolia.id),
		allowedOrigin: env.ALLOWED_ORIGIN ?? '*',
		rateLimit: Number(env.RATE_LIMIT ?? 10),
		rateWindowMs: 10 * 60 * 1000,
		maxDeadlineSeconds: 3600,
		staticDir: env.STATIC_DIR ?? '',
		world: worldConfigFromEnv(env)
	};
}

export function createRelayer(config: Config) {
	const chain: Chain = { ...sepolia, id: config.chainId };
	const transport = http(config.rpcUrl);
	const publicClient = createPublicClient({ chain, transport });
	const account = config.privateKey ? privateKeyToAccount(config.privateKey) : config.team;
	if (!account) throw new Error('set RELAYER_PRIVATE_KEY (or KINJO_TEAM on a local fork)');
	const wallet = createWalletClient({ chain, transport, account });
	const recent = new Map<string, number[]>();

	function rateLimited(owner: string): boolean {
		const now = Date.now();
		const times = (recent.get(owner) ?? []).filter((t) => now - t < config.rateWindowMs);
		if (times.length >= config.rateLimit) return true;
		times.push(now);
		recent.set(owner, times);
		return false;
	}

	/** Returns the tx hash. Throws an Error with a message fit for the user. */
	async function relay(request: RelayRequest): Promise<Hex> {
		const problem = validateRequest(request);
		if (problem) throw new Error(problem);
		const deadline = Number(request.deadline);
		if (deadline > Date.now() / 1000 + config.maxDeadlineSeconds) throw new Error('deadline too far in the future');
		if (rateLimited(request.owner.toLowerCase())) throw new Error('too many requests, try again in a few minutes');

		const fee = await publicClient.readContract({ address: config.onboarding, abi: onboardingAbi, functionName: 'fee' });
		const { functionName, args } = contractCall(request);
		try {
			const { request: tx } = await publicClient.simulateContract({
				address: config.onboarding,
				abi: onboardingAbi,
				functionName,
				args: args as never,
				value: fee,
				account: wallet.account
			});
			return await wallet.writeContract(tx);
		} catch (e) {
			throw new Error(revertReason(e));
		}
	}

	/** The owner's Kinjo account, or null if they have no name. */
	async function accountOf(owner: Address) {
		const [, , label, verifiedHuman] = await publicClient.readContract({
			address: config.onboarding,
			abi: onboardingAbi,
			functionName: 'accountOf',
			args: [owner]
		});
		return label ? { label, verifiedHuman } : null;
	}

	/** Registers label.verified.kinjo.eth for the owner. Only after a checked World ID proof. */
	async function setVerified(owner: Address): Promise<Hex> {
		try {
			const { request: tx } = await publicClient.simulateContract({
				address: config.onboarding,
				abi: onboardingAbi,
				functionName: 'setVerifiedHuman',
				args: [owner, true],
				account: wallet.account
			});
			return await wallet.writeContract(tx);
		} catch (e) {
			throw new Error(revertReason(e));
		}
	}

	return { relay, account: accountOf, setVerified, rateLimited, address: wallet.account.address };
}

function revertReason(e: unknown): string {
	if (e instanceof BaseError) {
		const revert = e.walk((x) => x instanceof ContractFunctionRevertedError);
		if (revert instanceof ContractFunctionRevertedError) return revert.data?.errorName ?? revert.shortMessage;
		return e.shortMessage;
	}
	return String(e);
}

async function readJson(req: IncomingMessage): Promise<unknown> {
	let body = '';
	for await (const chunk of req) {
		body += chunk;
		if (body.length > 50_000) throw new Error('request too large');
	}
	return JSON.parse(body);
}

const TYPES: Record<string, string> = {
	'.html': 'text/html; charset=utf-8',
	'.js': 'text/javascript',
	'.css': 'text/css',
	'.svg': 'image/svg+xml',
	'.json': 'application/json',
	'.png': 'image/png',
	'.txt': 'text/plain',
	'.wasm': 'application/wasm',
	'.webmanifest': 'application/manifest+json'
};

/** Serves the static app. Unknown paths get index.html (client-side routing). */
async function serveStatic(dir: string, url: string, res: ServerResponse) {
	const path = normalize(decodeURIComponent(url.split('?')[0])).replace(/^(\.\.[/\\])+/, '');
	for (const file of [join(dir, path), join(dir, 'index.html')]) {
		if (!file.startsWith(dir)) continue;
		try {
			const body = await readFile(file);
			res.setHeader('Content-Type', TYPES[extname(file)] ?? 'application/octet-stream');
			return res.writeHead(200).end(body);
		} catch {
			// directory or missing: try the next candidate
		}
	}
	res.writeHead(404).end('not found');
}

export function startServer(config: Config, port: number) {
	const relayer = createRelayer(config);
	const world = config.world ? createWorld(config.world, relayer) : null;
	const server = createServer(async (req: IncomingMessage, res: ServerResponse) => {
		const origins = config.allowedOrigin.split(',').map((o) => o.trim());
		const origin = req.headers.origin ?? '';
		if (origins.includes('*')) res.setHeader('Access-Control-Allow-Origin', '*');
		else if (origins.includes(origin)) res.setHeader('Access-Control-Allow-Origin', origin);
		res.setHeader('Vary', 'Origin');
		res.setHeader('Access-Control-Allow-Headers', 'content-type');
		const send = (status: number, data: object) =>
			res.setHeader('Content-Type', 'application/json').writeHead(status).end(JSON.stringify(data));

		if (req.method === 'OPTIONS') return send(204, {});
		if (req.method === 'GET' && req.url === '/health') {
			return send(200, { ok: true, relayer: relayer.address, onboarding: config.onboarding, chainId: config.chainId });
		}
		if (req.method === 'POST' && req.url === '/relay') {
			let body: RelayRequest | undefined;
			try {
				body = (await readJson(req)) as RelayRequest;
				const hash = await relayer.relay(body);
				console.log(`relayed ${body.action} for ${body.owner}: ${hash}`);
				return send(200, { hash });
			} catch (e) {
				console.log(`rejected ${body?.action ?? '?'} for ${body?.owner ?? '?'}: ${(e as Error).message}`);
				return send(400, { error: (e as Error).message });
			}
		}
		if (req.method === 'POST' && req.url?.startsWith('/world/')) {
			if (!world) return send(503, { error: 'World ID is not set up on this deployment' });
			let owner = '?';
			try {
				if (req.url === '/world/context') return send(200, world.context());
				if (req.url !== '/world/verify') return send(404, { error: 'not found' });
				const body = (await readJson(req)) as { owner?: string; proof?: object };
				owner = String(body.owner);
				if (relayer.rateLimited(owner.toLowerCase())) throw new Error('too many requests, try again in a few minutes');
				const result = await world.verify(owner, body.proof ?? {});
				console.log(`world verified ${owner}: ${result.hash ?? 'already verified'}`);
				return send(200, result);
			} catch (e) {
				console.log(`world rejected ${owner}: ${(e as Error).message}`);
				return send(400, { error: (e as Error).message });
			}
		}
		if (req.method === 'GET' && config.staticDir) return serveStatic(config.staticDir, req.url ?? '/', res);
		send(404, { error: 'not found' });
	});
	return new Promise<typeof server>((resolve) => server.listen(port, () => resolve(server)));
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
	const config = configFromEnv();
	const port = Number(process.env.PORT ?? 8787);
	const server = await startServer(config, port);
	console.log(`relayer on :${port} for ${config.onboarding} (chain ${config.chainId})`);
	process.on('SIGTERM', () => server.close());
}
