<script lang="ts">
	import { bytesToHex } from '@noble/hashes/utils.js';

	import Card from './Card.svelte';
	import { app } from '$lib/kinjo/state.svelte';

	let busy = $state(false);
	let error = $state('');
	let deviceName = $state('');

	async function run(action: () => Promise<unknown>) {
		busy = true;
		error = '';
		try {
			await action();
		} catch (e) {
			error = (e as Error).message;
		} finally {
			busy = false;
		}
	}

	const refresh = async () => (app.device = await app.deviceLink.info());

	const connect = () =>
		run(async () => {
			app.device = await app.deviceLink.connect();
			deviceName = app.device.name;
		});

	const saveName = () =>
		run(async () => {
			await app.deviceLink.setName(deviceName.trim());
			await refresh();
		});

	/** Pushes this laptop and every contact to the handheld. */
	const pushContacts = () =>
		run(async () => {
			if (!app.name) throw new Error('give this laptop a name first');
			await app.deviceLink.clearContacts();
			await app.deviceLink.addContact(app.name, app.node.pub, false);
			for (const c of app.contacts) {
				if (c.name !== app.device?.name && !c.revoked) await app.deviceLink.addContact(c.name, c.pub, !!c.verifiedHuman);
			}
			await refresh();
		});

	/** Lets this laptop open the handheld's messages (until ENS provides the key). */
	const trustDevice = () =>
		run(async () => {
			if (!app.device?.name) throw new Error('name the device first');
			app.upsertContact({ name: app.device.name, pub: app.device.pub });
		});

	const wipe = () =>
		run(async () => {
			if (!confirm('Wipe the handheld? It gets a new key and forgets its name and contacts.')) return;
			app.device = await app.deviceLink.wipe();
			deviceName = '';
		});
</script>

<Card title="Handheld over USB">
	{#if !app.device}
		<p class="text-sm text-neutral-600">Plug in the handheld and pick its port.</p>
		<button class="btn mt-3" disabled={busy} onclick={connect}>Connect device</button>
	{:else}
		<dl class="grid grid-cols-[auto_1fr] gap-x-3 gap-y-1 text-sm">
			<dt class="text-neutral-500">Name</dt>
			<dd>{app.device.name || '(none yet)'}</dd>
			<dt class="text-neutral-500">Public key</dt>
			<dd><code class="break-all text-xs">0x{bytesToHex(app.device.pub)}</code></dd>
			<dt class="text-neutral-500">Contacts</dt>
			<dd>{app.device.contacts}</dd>
		</dl>

		<label class="mt-4 block text-sm text-neutral-600" for="device-name">Device ENS name</label>
		<div class="mt-1 flex gap-2">
			<input id="device-name" class="input flex-1" bind:value={deviceName} placeholder="handheld.alice.kinjo.eth" />
			<button class="btn" disabled={busy} onclick={saveName}>Save</button>
		</div>

		<div class="mt-4 flex flex-wrap gap-2">
			<button class="btn" disabled={busy} onclick={pushContacts}>Push contacts</button>
			<button class="btn-secondary" disabled={busy} onclick={trustDevice}>Add to this laptop's contacts</button>
			<button class="btn-danger" disabled={busy} onclick={wipe}>Wipe device</button>
			<button class="btn-secondary" onclick={() => app.deviceLink.disconnect()}>Disconnect</button>
		</div>
	{/if}

	{#if error}<p class="mt-3 text-sm text-red-600">{error}</p>{/if}
	{#if app.deviceLog.length}
		<pre class="mt-3 max-h-32 overflow-auto rounded bg-neutral-100 p-2 text-xs">{app.deviceLog.join('\n')}</pre>
	{/if}
</Card>
