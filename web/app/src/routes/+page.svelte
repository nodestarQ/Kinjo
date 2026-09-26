<script lang="ts">
	import DevicePanel from '$lib/components/DevicePanel.svelte';
	import IdentityPanel from '$lib/components/IdentityPanel.svelte';
	import MeshPanel from '$lib/components/MeshPanel.svelte';
	import OwnerPanel from '$lib/components/OwnerPanel.svelte';
	import PortPicker from '$lib/components/PortPicker.svelte';
	import { serialHelperUrl } from '$lib/kinjo/config';
	import { app } from '$lib/kinjo/state.svelte';
	import { dev } from '$app/environment';

	// Dev only: /?preview fills the chat with sample lines to check the look.
	if (dev && typeof location !== 'undefined' && location.search.includes('preview') && !app.messages.length) {
		const t = Date.now() - 60_000;
		const wave: [number, number][] = Array.from({ length: 40 }, (_, i) => [40 + i * 6, 120 + Math.round(40 * Math.sin(i / 4))]);
		app.notices.push({ at: t, text: 'handheld.alice.kinjo.eth registered in ENS' });
		app.messages.push({ id: 1, at: t + 5000, from: 'handheld.alice.kinjo.eth', outgoing: false, status: 'verified', text: 'gm Tokyo' });
		app.messages.push({ id: 2, at: t + 9000, from: 'handheld.alice.kinjo.eth', outgoing: true, status: 'verified', text: 'gm! the relay only sees ciphertext' });
		app.messages.push({ id: 3, at: t + 15000, from: 'node1.bob.kinjo.eth', outgoing: false, status: 'verified', strokes: [{ color: 5, points: wave }, { color: 1, points: [[150, 60], [170, 80], [190, 60]] }] });
		app.notices.push({ at: t + 20000, text: 'handheld.alice.kinjo.eth revoked' });
		app.messages.push({ id: 4, at: t + 25000, from: 'handheld.alice.kinjo.eth', outgoing: false, status: 'revoked' });
	}
	import { serialSupported } from '$lib/kinjo/serial';
</script>

<main class="mx-auto max-w-6xl p-6">
	<header class="panel flex flex-wrap items-baseline gap-x-4 gap-y-1 px-4 py-3">
		<h1 class="text-3xl tracking-wider text-accent">Kinjo</h1>
		<p class="text-sm text-frame-dark">近所 · chat with your neighbors, no internet needed, trust from ENS</p>
	</header>

	{#if !serialSupported(serialHelperUrl)}
		<p class="mt-4 rounded bg-amber-100 p-3 text-sm text-amber-900">
			This browser can't talk to USB devices. Use Chrome or Edge.
		</p>
	{/if}

	<div class="mt-6 grid gap-6 lg:grid-cols-2">
		<div class="space-y-6">
			<OwnerPanel />
			<DevicePanel />
			<IdentityPanel />
		</div>
		<MeshPanel />
	</div>
	<PortPicker />
</main>
