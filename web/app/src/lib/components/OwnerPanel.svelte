<script lang="ts">
	import Card from './Card.svelte';
	import { app } from '$lib/kinjo/state.svelte';
	import { LABEL_PATTERN } from '$lib/kinjo/onboarding';
	import { ensConfig } from '$lib/kinjo/config';

	let error = $state('');
	let label = $state('');
	let deviceLabel = $state('handheld');
	let useLaptop = $state(false);

	// The suggested name follows the choice. A name typed by hand stays.
	$effect(() => {
		const suggested = useLaptop ? 'laptop' : 'handheld';
		if (deviceLabel === 'handheld' || deviceLabel === 'laptop' || !deviceLabel) deviceLabel = suggested;
	});
	let releaseLabel = $state('');

	const labelOk = $derived(LABEL_PATTERN.test(label));
	const deviceOk = $derived(LABEL_PATTERN.test(deviceLabel));
	const short = (a: string) => `${a.slice(0, 6)}…${a.slice(-4)}`;
	const sponsored = !!ensConfig.relayerUrl;

	/** Devices of this owner that we know about: contacts under their name plus this laptop. */
	const myDevices = $derived(
		app.owner?.label
			? app.contacts.filter((c) => c.name.endsWith(`.${app.owner!.label}.kinjo.eth`) && !c.revoked).map((c) => c.name.split('.')[0])
			: []
	);

	async function run(action: () => Promise<unknown>) {
		error = '';
		try {
			await action();
		} catch (e) {
			error = (e as Error).message.split('\n')[0];
		}
	}
</script>

<Card title="Your name in ENS">
	{#if !app.owner}
		<p class="text-sm text-neutral-600">
			Connect a wallet to claim a free name under <b>kinjo.eth</b> and register devices.
			{sponsored ? 'You only sign messages, no Sepolia ETH needed.' : 'You pay the gas yourself.'}
		</p>
		<button class="btn mt-3" onclick={() => run(() => app.connectWallet())}>Connect wallet</button>
	{:else}
		<p class="text-sm text-neutral-600">
			Wallet {short(app.owner.address)}
			{#if app.owner.label}
				· <b>{app.owner.label}.kinjo.eth</b>
				{#if app.owner.verifiedHuman}<span class="ml-1 rounded bg-blue-100 px-1.5 text-xs text-blue-800">verified human</span>{/if}
			{/if}
		</p>

		<div class="mt-3 flex gap-2">
			<label class="flex items-center gap-1 text-sm"><input type="radio" bind:group={useLaptop} value={false} /> the handheld on USB</label>
			<label class="flex items-center gap-1 text-sm"><input type="radio" bind:group={useLaptop} value={true} /> this laptop (web app)</label>
		</div>
		<label class="mt-2 block text-sm text-neutral-600" for="device-label">Device name</label>
		<input id="device-label" class="input w-full" bind:value={deviceLabel} placeholder="handheld" />

		{#if !app.owner.label}
			<label class="mt-3 block text-sm text-neutral-600" for="owner-label">Your name</label>
			<div class="flex items-center gap-1">
				<input id="owner-label" class="input flex-1" bind:value={label} placeholder="alice" />
				<span class="text-sm text-neutral-500">.kinjo.eth</span>
			</div>
			<p class="mt-1 text-xs text-neutral-500">
				Becomes <b>{deviceOk ? deviceLabel : '…'}.{labelOk ? label : '…'}.kinjo.eth</b>. Lowercase a-z, 0-9 and "-".
			</p>
			<button class="btn mt-3" disabled={!labelOk || !deviceOk || !!app.busy} onclick={() => run(() => app.join(label, deviceLabel, useLaptop))}>
				Join
			</button>
		{:else}
			<p class="mt-1 text-xs text-neutral-500">Becomes <b>{deviceOk ? deviceLabel : '…'}.{app.owner.label}.kinjo.eth</b>.</p>
			<button class="btn mt-3" disabled={!deviceOk || !!app.busy} onclick={() => run(() => app.addDevice(deviceLabel, useLaptop))}>
				Register device
			</button>

			{#if myDevices.length}
				<h3 class="mt-5 font-medium">Your devices</h3>
				<ul class="mt-1 space-y-1 text-sm">
					{#each myDevices as d (d)}
						<li class="flex items-center gap-2">
							{d}.{app.owner.label}.kinjo.eth
							<button class="btn-danger ml-auto" disabled={!!app.busy} onclick={() => run(() => app.revoke(d))}>Revoke</button>
						</li>
					{/each}
				</ul>
			{/if}

			<button
				class="btn-secondary mt-4"
				disabled={!!app.busy}
				onclick={() => confirm(`Reset the demo? Revokes ${deviceLabel}, wipes the handheld, forgets the wallet.`) && run(() => app.resetDemo(deviceLabel))}
			>
				Reset demo
			</button>
		{/if}

		{#if app.isTeam}
			<details class="mt-4 text-sm">
				<summary class="cursor-pointer text-neutral-600">Team: release a name</summary>
				<div class="mt-2 flex gap-2">
					<input class="input flex-1" bind:value={releaseLabel} placeholder="alice" />
					<button class="btn-danger" disabled={!!app.busy} onclick={() => run(() => app.release(releaseLabel.trim()))}>Release</button>
				</div>
			</details>
		{/if}
	{/if}

	{#if app.busy}<p class="mt-3 text-sm text-blue-700">{app.busy}…</p>{/if}
	{#if error}<p class="mt-3 text-sm text-red-600">{error}</p>{/if}
</Card>
