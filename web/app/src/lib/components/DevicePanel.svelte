<script lang="ts">
	import { bytesToHex } from '@noble/hashes/utils.js';

	import Card from './Card.svelte';
	import DeviceRow from './DeviceRow.svelte';
	import { LABEL_PATTERN } from '$lib/kinjo/onboarding';
	import { app } from '$lib/kinjo/state.svelte';
	import { shortName, tabColor } from '$lib/kinjo/tabs';

	let busy = $state(false);
	let error = $state('');
	let deviceLabel = $state('handheld');

	const label = $derived(app.owner?.label ?? '');
	/** The connected device is one of the owner's registered devices. */
	const registered = $derived(!!app.device?.name && app.myDevices.includes(app.device.name));
	const locked = $derived(!app.hasWallet || !app.online || !!app.busy || busy);

	const HINTS: Record<string, string> = {
		LabelAlreadyRegistered: 'this name is already registered under your name. Pick another one or revoke the old device first.'
	};

	async function run(action: () => Promise<unknown>) {
		busy = true;
		error = '';
		try {
			await action();
		} catch (e) {
			const msg = (e as Error).message.split('\n')[0];
			const hint = Object.entries(HINTS).find(([k]) => msg.includes(k));
			error = hint ? `${hint[0]}: ${hint[1]}` : msg;
		} finally {
			busy = false;
		}
	}

	const connect = () => run(() => app.connectDevice());

	const wipe = () =>
		run(async () => {
			if (!confirm('Wipe the handheld? It gets a new key and forgets its name and contacts.')) return;
			app.device = await app.deviceLink.wipe();
		});
</script>

<Card title="Add a new device">
	{#if !app.device}
		<ol class="list-decimal space-y-1 pl-5 text-sm">
			<li>Plug the handheld into this laptop with USB.</li>
			<li>Connect it and pick its port.</li>
			<li>Give it a name and register it. It gets its name and contacts right after.</li>
		</ol>
		<button class="btn mt-3" disabled={busy} onclick={connect}>Connect device</button>
	{:else}
		<dl class="grid grid-cols-[auto_1fr] gap-x-3 gap-y-1 text-sm">
			<dt class="text-frame-dark">Name</dt>
			<dd>
				{#if app.device.name}
					<span class="tab rounded-sm" style="background:{tabColor(app.device.name)}">{shortName(app.device.name)}</span>
				{:else}(new, not registered){/if}
			</dd>
			<dt class="text-frame-dark">Public key</dt>
			<dd><code class="break-all text-xs">0x{bytesToHex(app.device.pub)}</code></dd>
			<dt class="text-frame-dark">Contacts</dt>
			<dd>{app.device.contacts}</dd>
		</dl>

		{#if registered}
			<ul class="mt-3"><DeviceRow name={app.device.name} note="connected" run={(a) => run(a)} /></ul>
		{:else}
			<div class="mt-3 flex flex-wrap items-center gap-1">
				<input class="input w-32" bind:value={deviceLabel} />
				<span class="text-sm text-frame-dark">.{label}.kinjo.eth</span>
				<button
					class="btn w-full sm:ml-auto sm:w-auto"
					disabled={!LABEL_PATTERN.test(deviceLabel) || locked}
					onclick={() => run(() => app.addDevice(deviceLabel, false))}
				>
					Register device
				</button>
			</div>
			{#if !app.hasWallet}<p class="mt-1 text-xs text-frame-dark">Connect your wallet under "This laptop" to register.</p>{/if}
		{/if}

		<div class="mt-4 flex flex-wrap gap-2">
			<button class="btn-secondary" disabled={busy || !app.name} onclick={() => run(() => app.syncDevice())}>Sync with ENS</button>
			<button class="btn-danger" disabled={busy} onclick={wipe}>Wipe device</button>
			<button class="btn-secondary" onclick={() => app.deviceLink.disconnect()}>Disconnect</button>
		</div>
	{/if}

	{#if app.busy}<p class="mt-3 text-sm text-blue-700">{app.busy}…</p>{/if}
	{#if error}<p class="mt-3 text-sm text-red-600">{error}</p>{/if}
</Card>
