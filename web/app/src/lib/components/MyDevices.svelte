<script lang="ts">
	import Card from './Card.svelte';
	import DeviceRow from './DeviceRow.svelte';
	import { LABEL_PATTERN } from '$lib/kinjo/onboarding';
	import { app } from '$lib/kinjo/state.svelte';

	let { section }: { section: 'this' | 'others' } = $props();

	let error = $state('');
	let laptopLabel = $state('laptop');
	const label = $derived(app.owner?.label ?? '');
	const locked = $derived(!app.hasWallet || !app.online || !!app.busy);

	/** Registered devices other than this laptop, read from the chain. */
	const others = $derived(app.myDevices.filter((d) => d !== app.name));
	$effect(() => {
		if (app.online) app.refreshDevices().catch(() => {});
	});

	const HINTS: Record<string, string> = {
		LabelAlreadyRegistered: 'this name is already registered under your name. Pick another one or revoke the old device first.',
		NotJoined: 'this wallet has no name yet.',
		InvalidSignature: 'the signature does not match this wallet.'
	};

	function run(action: () => Promise<unknown>) {
		error = '';
		action().catch((e: Error) => {
			const msg = e.message.split('\n')[0];
			const hint = Object.entries(HINTS).find(([k]) => msg.includes(k));
			error = hint ? `${hint[0]}: ${hint[1]}` : msg;
		});
	}
</script>

{#if section === 'this'}
	<Card title="This laptop">
		{#if !app.hasWallet}
			<p class="text-sm text-frame-dark">Registering, renaming and revoking need your wallet.</p>
			<button class="btn mt-2" disabled={!app.online} onclick={() => run(() => app.connectWallet())}>Connect wallet</button>
		{/if}
		{#if app.name}
			<ul class="mt-2"><DeviceRow name={app.name} note="the browser you're using" {run} /></ul>
		{:else}
			<p class="mt-2 text-sm">This browser isn't a registered device yet. The chat needs it.</p>
			<div class="mt-2 flex flex-wrap items-center gap-1">
				<input class="input w-32" bind:value={laptopLabel} />
				<span class="text-sm text-frame-dark">.{label}.kinjo.eth</span>
				<button
					class="btn w-full sm:ml-auto sm:w-auto"
					disabled={!LABEL_PATTERN.test(laptopLabel) || locked}
					onclick={() => run(() => app.addDevice(laptopLabel, true))}
				>
					Register this laptop
				</button>
			</div>
		{/if}
		{#if app.busy}<p class="mt-3 text-sm text-blue-700">{app.busy}…</p>{/if}
		{#if error}<p class="mt-3 text-sm text-red-600">{error}</p>{/if}
	</Card>
{:else}
	<Card title="Your other devices">
		<ul class="space-y-2">
			{#each others as d (d)}
				<DeviceRow name={d} {run} />
			{:else}
				<li class="text-sm text-frame-dark">None yet. Add one above.</li>
			{/each}
		</ul>
		<button
			class="btn-secondary mt-5"
			disabled={!!app.busy}
			onclick={() => confirm('Reset the demo? Revokes the connected handheld, wipes it and signs you out.') && run(() => app.resetDemo(app.device?.name.split('.')[0] || 'handheld'))}
		>
			Reset demo
		</button>
		{#if error}<p class="mt-3 text-sm text-red-600">{error}</p>{/if}
	</Card>
{/if}
