<script lang="ts">
	import BridgeBar from './BridgeBar.svelte';
	import ChatLine from './ChatLine.svelte';
	import DrawPad from './DrawPad.svelte';
	import { app } from '$lib/kinjo/state.svelte';
	import { shortName, tabColor } from '$lib/kinjo/tabs';
	import type { Message } from '$lib/kinjo/mesh';
	import { MAX_TEXT, type ColoredStroke } from '$lib/kinjo/protocol';

	// One chat: every message is sealed for exactly one contact.
	let { name }: { name: string } = $props();

	let error = $state('');
	let text = $state('');
	let drawOpen = $state(false);
	let strokes = $state<ColoredStroke[]>([]);

	// The limit is in bytes (SPEC §6): Japanese takes 3 per character, emoji 4.
	const bytes = $derived(new TextEncoder().encode(text).length);
	const tooLong = $derived(bytes > MAX_TEXT);

	const contact = $derived(app.contacts.find((c) => c.name === name));
	const color = $derived(contact && !contact.revoked ? tabColor(name) : '#9aa5b1');

	// Messages with this contact and system lines about it, oldest first.
	type Entry = { at: number; key: string } & ({ message: Message } | { notice: string });
	const timeline = $derived(
		[
			...app.messages
				.filter((m) => m.from === name)
				.map((m): Entry => ({ at: m.at, key: `m${m.id}${m.outgoing ? 'o' : 'i'}`, message: m })),
			...app.notices
				.filter((n) => n.text.includes(name))
				.map((n, i): Entry => ({ at: n.at, key: `n${i}${n.at}`, notice: n.text }))
		].sort((a, b) => a.at - b.at)
	);

	let chat: HTMLUListElement | undefined = $state();
	$effect(() => {
		void timeline.length;
		app.markRead(name); // open chat: everything in it counts as read
		chat?.scrollTo({ top: chat.scrollHeight, behavior: 'smooth' });
	});

	async function send() {
		error = '';
		try {
			if (!contact || contact.revoked) throw new Error('not a contact you can write to');
			if (!text && !strokes.length) return;
			if (tooLong) throw new Error(`text is ${bytes} bytes, the limit is ${MAX_TEXT}`);
			await app.sendNote(contact, text, $state.snapshot(strokes) as ColoredStroke[]);
			text = '';
			strokes = [];
			drawOpen = false;
		} catch (e) {
			error = (e as Error).message;
		}
	}
</script>

<section class="panel p-3 sm:p-4">
	<div class="flex items-center gap-2">
		<a href="/chat" class="btn-secondary">&lt; Chats</a>
		<span class="tab rounded-sm" class:line-through={contact?.revoked} style="background:{color}">{shortName(name)}</span>
		{#if contact?.verifiedHuman}<span class="rounded bg-blue-100 px-1.5 text-xs text-blue-800">human</span>{/if}
		{#if contact?.revoked}<span class="rounded bg-red-100 px-1.5 text-xs text-red-800">REVOKED</span>{/if}
	</div>
	<p class="mt-1 truncate text-xs text-frame-dark">{contact ? name : 'Not a contact: no key to write to.'}</p>

	<div class="mt-3"><BridgeBar /></div>

	<div class="mt-3 rounded-sm border-2 border-frame bg-console p-2">
		<ul bind:this={chat} class="h-[55vh] min-h-64 space-y-2 overflow-y-auto pr-1">
			{#each timeline as e (e.key)}
				{#if 'message' in e}
					<ChatLine m={e.message} me={app.name} />
				{:else}
					<li class="text-center text-xs text-frame-dark">▶ {e.notice} ◀</li>
				{/if}
			{:else}
				<li class="pt-8 text-center text-sm text-frame-dark">Nothing here yet. Say hi!</li>
			{/each}
		</ul>
	</div>

	{#if contact && !contact.revoked}
		<div class="mt-3 flex items-stretch">
			<span class="tab hidden items-center rounded-l-sm rounded-r-none sm:flex" style="background:{tabColor(app.name || 'this laptop')}">{shortName(app.name) || 'you'}</span>
			<div class="flex min-w-0 flex-1 items-center gap-2 rounded-sm border-2 bg-paper sm:rounded-l-none sm:border-l-0 px-2 py-1" style="border-color:{tabColor(app.name || 'this laptop')}">
				<input class="min-w-0 flex-1 bg-transparent outline-none" bind:value={text} placeholder="Write something…" onkeydown={(e) => e.key === 'Enter' && send()} />
				<span class="shrink-0 text-xs tabular-nums {tooLong ? 'font-bold text-red-600' : 'text-frame-dark'}" title="bytes used of {MAX_TEXT}">{bytes}/{MAX_TEXT}</span>
				<button class="btn-secondary" onclick={() => (drawOpen = !drawOpen)} aria-label="draw">✎</button>
				<button class="btn" disabled={!app.bridgeConnected || tooLong} onclick={send}>Send</button>
			</div>
		</div>
		{#if drawOpen}
			<div class="mt-2 rounded-sm border-2 border-frame bg-console p-2">
				<DrawPad bind:strokes />
				<p class="mt-1 text-xs text-frame-dark">Text and drawing go out together as one note with Send.</p>
			</div>
		{/if}
	{/if}
	{#if error}<p class="mt-3 text-sm text-red-600">{error}</p>{/if}
</section>
