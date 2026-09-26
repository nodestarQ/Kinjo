<script lang="ts">
	import { dev } from '$app/environment';

	import MeshPanel from '$lib/components/MeshPanel.svelte';
	import RequireSignIn from '$lib/components/RequireSignIn.svelte';
	import { app } from '$lib/kinjo/state.svelte';

	// Dev only: /chat?preview fills the chat with sample lines to check the look.
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
</script>

<RequireSignIn>
	{#if !app.name}
		<p class="panel p-4 text-sm">This browser isn't a registered device yet. <a class="text-accent underline" href="/devices">Register it on the Devices page</a>.</p>
	{:else}
		<div class="mx-auto max-w-3xl"><MeshPanel /></div>
	{/if}
</RequireSignIn>
