// Build-time settings from web/app/.env (see .env.example).

import {
	PUBLIC_KINJO_CHAIN_ID,
	PUBLIC_KINJO_ONBOARDING,
	PUBLIC_KINJO_RPC_URL,
	PUBLIC_KINJO_TEAM,
	PUBLIC_RELAYER_URL,
	PUBLIC_SERIAL_HELPER
} from '$env/static/public';
import type { Address } from 'viem';

import type { EnsConfig } from './ens';

export const ensConfig: EnsConfig = {
	chainId: Number(PUBLIC_KINJO_CHAIN_ID),
	rpcUrl: PUBLIC_KINJO_RPC_URL,
	onboarding: PUBLIC_KINJO_ONBOARDING as Address,
	relayerUrl: PUBLIC_RELAYER_URL.replace(/\/$/, '')
};

export const teamAddress = PUBLIC_KINJO_TEAM.toLowerCase();

/** Local serial helper (firmware/tools/serial_helper.py). Empty: use the browser's Web Serial. */
export const serialHelperUrl = PUBLIC_SERIAL_HELPER.replace(/\/$/, '');
