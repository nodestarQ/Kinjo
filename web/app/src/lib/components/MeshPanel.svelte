<script lang="ts">
	import { hexToBytes } from '@noble/hashes/utils.js';

	import Card from './Card.svelte';
	import ChatLine from './ChatLine.svelte';
	import DrawPad from './DrawPad.svelte';
	import { app } from '$lib/kinjo/state.svelte';
	import { shortName, tabColor } from '$lib/kinjo/tabs';
	import type { Message } from '$lib/kinjo/mesh';
	import type { ColoredStroke } from '$lib/kinjo/protocol';

	let error = $state('');
	let to = $state('');
	let text = $state('');
	let newName = $state('');
	let newPub = $state('');
	let ensName = $state('');
	let drawOpen = $state(false);
	let strokes = $state<ColoredStroke[]>([]);

	// One timeline: messages and system lines, oldest first like a chat.
	type Entry = { at: number; key: string } & ({ message: Message } | { notice: string });
	const timeline = $derived(
		[
			...app.messages.map((m): Entry => ({ at: m.at, key: `m${m.id}${m.outgoing ? 'o' : 'i'}`, message: m })),
			...app.notices.map((n, i): Entry => ({ at: n.at, key: `n${i}${n.at}`, notice: n.text }))
		].sort((a, b) => a.at - b.at)
	);

	let chat: HTMLUListElement | undefined = $state();
	$effect(() => {
		void timeline.length;
		chat?.scrollTo({ top: chat.scrollHeight, behavior: 'smooth' });
	});

	async function run(action: () => Promise<unknown>) {
		error = '';
		try {
			await action();
		} catch (e) {
			error = (e as Error).message;
		}
	}

	const send = () =>
		run(async () => {
			const contact = app.contacts.find((c) => c.name === to);
			if (!contact) throw new Error('pick a contact');
			if (!text && !strokes.length) return;
			await app.sendNote(contact, text, $state.snapshot(strokes) as ColoredStroke[]);
			text = '';
			strokes = [];
		});

	const addContact = () =>
		run(async () => {
			const pub = hexToBytes(newPub.trim().replace(/^0x/, ''));
			if (pub.length !== 32 || !newName.trim()) throw new Error('need a name and a 32-byte key');
			app.upsertContact({ name: newName.trim(), pub });
			newName = newPub = '';
		});

	const addByEns = () =>
		run(async () => {
			await app.addContactByName(ensName);
			ensName = '';
		});

	const time = (t: number) => new Date(t).toLocaleTimeString();
</script>

<Card title="Mesh (radio bridge)">
	{#if !app.bridgeConnected}
		<p class="text-sm text-frame-dark">Plug in the radio bridge XIAO and pick its port.</p>
		<button class="btn mt-3" onclick={() => run(() => app.connectBridge())}>Connect bridge</button>
	{:else}
		<div class="flex items-center gap-2 text-sm">
			<span class="h-2 w-2 rounded-full bg-green-500"></span> Bridge connected
			<button class="btn-secondary ml-auto" onclick={() => app.disconnectBridge()}>Disconnect</button>
		</div>
	{/if}

	<div class="mt-4 rounded-sm border-2 border-frame bg-console p-2">
		<div class="mb-2 flex items-center justify-between text-xs text-frame-dark">
			<span>ROOM · kinjo.eth</span>
			<span>{app.contacts.filter((c) => !c.revoked).length + 1} here</span>
		</div>
		<ul bind:this={chat} class="h-96 space-y-2 overflow-y-auto pr-1">
			{#each timeline as e (e.key)}
				{#if 'message' in e}
					<ChatLine m={e.message} me={app.name} />
				{:else}
					<li class="text-center text-xs text-frame-dark">▶ {e.notice} ◀</li>
				{/if}
			{:else}
				<li class="pt-8 text-center text-sm text-frame-dark">Nobody has said anything yet.</li>
			{/each}
		</ul>
	</div>
	<div class="mt-3 flex items-stretch">
		<span class="tab flex items-center" style="background:{tabColor(app.name || 'this laptop')}">{shortName(app.name) || 'you'}</span>
		<div class="flex flex-1 items-center gap-2 rounded-r-sm border-2 border-l-0 bg-paper px-2 py-1" style="border-color:{tabColor(app.name || 'this laptop')}">
			<select class="input py-0" bind:value={to}>
				<option value="">to…</option>
				{#each app.contacts.filter((c) => !c.revoked) as c (c.name)}<option value={c.name}>{shortName(c.name)}</option>{/each}
			</select>
			<input class="min-w-0 flex-1 bg-transparent outline-none" bind:value={text} maxlength="200" placeholder="Write something…" onkeydown={(e) => e.key === 'Enter' && send()} />
			<button class="btn-secondary" onclick={() => (drawOpen = !drawOpen)} aria-label="draw">✎</button>
			<button class="btn" disabled={!app.bridgeConnected} onclick={send}>Send</button>
		</div>
	</div>
	{#if drawOpen}
		<div class="mt-2 rounded-sm border-2 border-frame bg-console p-2">
			<DrawPad bind:strokes />
			<p class="mt-1 text-xs text-frame-dark">Text and drawing go out together as one note with Send.</p>
		</div>
	{/if}

	<div class="mt-5 flex items-center">
		<h3 class="font-medium">Contacts</h3>
		<button class="btn-secondary ml-auto" onclick={() => run(() => app.syncEns())}>Sync ENS</button>
	</div>
	<p class="text-xs text-frame-dark">Keys come from ENS and are checked again every 10 s.</p>
	<div class="mt-2 flex gap-2">
		<input class="input flex-1" bind:value={ensName} placeholder="handheld.bob.kinjo.eth" onkeydown={(e) => e.key === 'Enter' && addByEns()} />
		<button class="btn" onclick={addByEns}>Add</button>
	</div>
	<ul class="mt-2 space-y-1 text-sm">
		{#each app.contacts as c (c.name)}
			<li class="flex items-center gap-2">
				<span class="tab rounded-sm" class:line-through={c.revoked} style="background:{c.revoked ? '#9aa5b1' : tabColor(c.name)}">{shortName(c.name)}</span>
				{#if c.verifiedHuman}<span class="rounded bg-blue-100 px-1.5 text-xs text-blue-800">human</span>{/if}
				{#if c.revoked}<span class="rounded bg-red-100 px-1.5 text-xs text-red-800">REVOKED</span>{/if}
				<button class="ml-auto text-xs text-frame-dark hover:text-red-600" onclick={() => app.removeContact(c.name)}>remove</button>
			</li>
		{:else}
			<li class="text-frame-dark">No contacts yet.</li>
		{/each}
	</ul>
	<details class="mt-2 text-sm">
		<summary class="cursor-pointer text-frame-dark">Add a contact by key (without ENS)</summary>
		<div class="mt-2 flex flex-col gap-2">
			<input class="input" bind:value={newName} placeholder="handheld.bob.kinjo.eth" />
			<input class="input" bind:value={newPub} placeholder="0x… public key" />
			<button class="btn self-start" onclick={addContact}>Add</button>
		</div>
	</details>

	{#if error}<p class="mt-3 text-sm text-red-600">{error}</p>{/if}

	<h3 class="mt-5 font-medium">Packets seen by the bridge</h3>
	<p class="text-xs text-frame-dark">What any relay sees: headers and ciphertext.</p>
	<div class="mt-2 max-h-48 overflow-auto font-mono text-xs">
		{#each app.packets as p, i (i)}
			<div class:text-frame={!p.forMe}>
				{time(p.at)} {p.fromMac} id={p.header.messageId.toString(16).padStart(8, '0')}
				{p.header.source.toString(16).padStart(8, '0')}→{p.header.destination.toString(16).padStart(8, '0')}
				ttl={p.header.ttl} {p.header.fragIndex + 1}/{p.header.fragCount} {p.size}B {p.preview}…
			</div>
		{/each}
	</div>
	{#if app.bridgeLog.length}
		<pre class="mt-3 max-h-24 overflow-auto rounded bg-console p-2 text-xs">{app.bridgeLog.join('\n')}</pre>
	{/if}
</Card>
