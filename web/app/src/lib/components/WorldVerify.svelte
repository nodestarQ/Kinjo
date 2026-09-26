<script lang="ts">
	import QRCode from 'qrcode';

	import { ensConfig } from '$lib/kinjo/config';
	import { app } from '$lib/kinjo/state.svelte';
	import { WorldError, requestProof, submitProof } from '$lib/kinjo/world';

	// idle -> scan (QR shown, waiting for World App) -> checking (relayer + tx) -> done, or failed.
	let phase = $state<'idle' | 'scan' | 'checking' | 'failed'>('idle');
	let uri = $state('');
	let qr = $state('');
	let staging = $state(false);
	let error = $state('');
	let canRetry = $state(true);
	let abort: AbortController | null = null;
	let copied = $state(false);
	let linkInput: HTMLInputElement | undefined = $state();

	/** Clipboard API first. It can be blocked (no permission, plain http), so fall back to a selected field. */
	async function copy() {
		copied = false;
		try {
			await navigator.clipboard.writeText(uri);
			copied = true;
		} catch {
			linkInput?.select();
			copied = document.execCommand('copy');
		}
	}

	async function start() {
		const owner = app.owner?.address;
		if (!owner) return;
		abort = new AbortController();
		error = '';
		try {
			const req = await requestProof(ensConfig.relayerUrl, owner, abort.signal);
			uri = req.uri;
			staging = req.environment === 'staging';
			qr = await QRCode.toDataURL(req.uri, { margin: 1, width: 220 });
			phase = 'scan';
			const proof = await req.result;
			phase = 'checking';
			const { hash } = await submitProof(ensConfig.relayerUrl, owner, proof);
			await app.afterBadge(hash);
			phase = 'idle';
		} catch (e) {
			error = (e as Error).message.split('\n')[0];
			canRetry = !(e instanceof WorldError) || e.retry;
			phase = 'failed';
		}
	}

	function cancel() {
		abort?.abort();
		phase = 'idle';
	}
</script>

{#if app.owner?.verifiedHuman}
	<p class="text-sm">
		<span class="rounded bg-blue-100 px-1.5 text-blue-800">verified human</span>
		Your badge is <b>{app.owner.label}.verified.kinjo.eth</b>. Your devices show it to everyone who has you as a contact.
	</p>
{:else}
	<p class="text-sm text-frame-dark">
		Prove you're a unique human with World ID. Kinjo then registers <b>{app.owner?.label}.verified.kinjo.eth</b> for you and
		others can filter for real people. One human, one name. World ID only tells Kinjo that you're a unique human, nothing else.
		Optional.
	</p>

	{#if phase === 'scan'}
		<div class="mt-3 flex flex-col items-center gap-2 rounded-sm border-2 border-frame bg-paper p-3 text-center">
			<img src={qr} alt="World ID QR code" class="h-52 w-52" />
			<p class="text-sm">Scan with World App, or <a class="text-accent underline" href={uri}>open World App</a> on this phone.</p>
			{#if staging}
				<p class="text-xs text-frame-dark">
					Testing: paste this link into the <a class="underline" href="https://simulator.worldcoin.org" target="_blank" rel="noreferrer">simulator</a>.
				</p>
				<div class="flex w-full gap-2">
					<input bind:this={linkInput} class="input min-w-0 flex-1 text-xs" readonly value={uri} onfocus={(e) => e.currentTarget.select()} />
					<button class="btn-secondary" onclick={copy}>{copied ? 'Copied' : 'Copy'}</button>
				</div>
			{/if}
			<p class="text-xs text-frame-dark">Waiting for World App…</p>
			<button class="btn-secondary" onclick={cancel}>Cancel</button>
		</div>
	{:else if phase === 'checking'}
		<p class="mt-3 text-sm text-blue-700">Checking the proof and registering your badge…</p>
	{:else}
		<button class="btn mt-3" disabled={!app.online || (phase === 'failed' && !canRetry)} onclick={start}>
			{phase === 'failed' ? 'Try again' : 'Verify with World ID'}
		</button>
		{#if !app.online}<p class="mt-1 text-xs text-frame-dark">Needs internet.</p>{/if}
	{/if}
	{#if phase === 'failed'}<p class="mt-2 text-sm text-red-600">Not verified: {error}</p>{/if}
{/if}
