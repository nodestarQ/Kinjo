// Relayer: submits owner-signed KinjoOnboarding requests and pays the gas (docs/onboarding.md).
// POST /relay checks the request, simulates the call and sends it from the team wallet.

import { createServer, type IncomingMessage, type ServerResponse } from 'node:http';
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

export interface Config {
	rpcUrl: string;
	onboarding: Address;
	/** Production: the team wallet key. Local fork: leave empty and set `team` (anvil impersonates it). */
	privateKey?: Hex;
	team?: Address;
	chainId: number;
	allowedOrigin: string;
	/** Requests per owner per window. */
	rateLimit: number;
	rateWindowMs: number;
	/** Longest accepted deadline, so signatures can't be hoarded. */
	maxDeadlineSeconds: number;
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
		maxDeadlineSeconds: 3600
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

	return { relay, address: wallet.account.address };
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
		if (body.length > 10_000) throw new Error('request too large');
	}
	return JSON.parse(body);
}

export function startServer(config: Config, port: number) {
	const relayer = createRelayer(config);
	const server = createServer(async (req: IncomingMessage, res: ServerResponse) => {
		res.setHeader('Access-Control-Allow-Origin', config.allowedOrigin);
		res.setHeader('Access-Control-Allow-Headers', 'content-type');
		res.setHeader('Content-Type', 'application/json');
		const send = (status: number, data: object) => res.writeHead(status).end(JSON.stringify(data));

		if (req.method === 'OPTIONS') return send(204, {});
		if (req.method === 'GET' && req.url === '/health') {
			return send(200, { ok: true, relayer: relayer.address, onboarding: config.onboarding, chainId: config.chainId });
		}
		if (req.method === 'POST' && req.url === '/relay') {
			try {
				const hash = await relayer.relay((await readJson(req)) as RelayRequest);
				return send(200, { hash });
			} catch (e) {
				return send(400, { error: (e as Error).message });
			}
		}
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
