<script lang="ts">
	import { hexToBytes } from '@noble/hashes/utils.js';

	import Card from './Card.svelte';
	import Drawing from './Drawing.svelte';
	import { app } from '$lib/kinjo/state.svelte';
	import type { MessageStatus } from '$lib/kinjo/mesh';

	let error = $state('');
	let to = $state('');
	let text = $state('');
	let newName = $state('');
	let newPub = $state('');
	let ensName = $state('');

	const statusClass: Record<MessageStatus, string> = {
		verified: 'bg-green-100 text-green-800',
		revoked: 'bg-red-100 text-red-800',
		'unknown sender': 'bg-amber-100 text-amber-800',
		'failed to open': 'bg-red-100 text-red-800'
	};

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
			await app.sendText(contact, text);
			text = '';
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
		<p class="text-sm text-neutral-600">Plug in the radio bridge XIAO and pick its port.</p>
		<button class="btn mt-3" onclick={() => run(() => app.connectBridge())}>Connect bridge</button>
	{:else}
		<div class="flex items-center gap-2 text-sm">
			<span class="h-2 w-2 rounded-full bg-green-500"></span> Bridge connected
			<button class="btn-secondary ml-auto" onclick={() => app.disconnectBridge()}>Disconnect</button>
		</div>
	{/if}

	<h3 class="mt-5 font-medium">Messages</h3>
	<ul class="mt-2 space-y-2">
		{#each app.messages as m (m.id + (m.outgoing ? 'o' : 'i'))}
			<li class="rounded border border-neutral-200 p-2 text-sm" class:ml-8={m.outgoing}>
				<div class="flex items-center gap-2 text-xs text-neutral-500">
					<span>{m.outgoing ? `to ${m.from}` : m.from}</span>
					{#if !m.outgoing}<span class="rounded px-1.5 py-0.5 {statusClass[m.status]}">{m.status}</span>{/if}
					<span class="ml-auto">{time(m.at)}</span>
				</div>
				{#if m.text !== undefined}<p class="mt-1">{m.text}</p>{/if}
				{#if m.strokes}<div class="mt-1"><Drawing strokes={m.strokes} /></div>{/if}
			</li>
		{:else}
			<li class="text-sm text-neutral-500">No messages yet.</li>
		{/each}
	</ul>

	<div class="mt-3 flex gap-2">
		<select class="input" bind:value={to}>
			<option value="">to…</option>
			{#each app.contacts.filter((c) => !c.revoked) as c (c.name)}<option value={c.name}>{c.name}</option>{/each}
		</select>
		<input class="input flex-1" bind:value={text} maxlength="200" placeholder="Message" onkeydown={(e) => e.key === 'Enter' && send()} />
		<button class="btn" disabled={!app.bridgeConnected} onclick={send}>Send</button>
	</div>

	<div class="mt-5 flex items-center">
		<h3 class="font-medium">Contacts</h3>
		<button class="btn-secondary ml-auto" onclick={() => run(() => app.syncEns())}>Sync ENS</button>
	</div>
	<p class="text-xs text-neutral-500">Keys come from ENS and are checked again every 10 s.</p>
	<div class="mt-2 flex gap-2">
		<input class="input flex-1" bind:value={ensName} placeholder="handheld.bob.kinjo.eth" onkeydown={(e) => e.key === 'Enter' && addByEns()} />
		<button class="btn" onclick={addByEns}>Add</button>
	</div>
	<ul class="mt-2 space-y-1 text-sm">
		{#each app.contacts as c (c.name)}
			<li class="flex items-center gap-2">
				<span class:line-through={c.revoked}>{c.name}</span>
				{#if c.verifiedHuman}<span class="rounded bg-blue-100 px-1.5 text-xs text-blue-800">human</span>{/if}
				{#if c.revoked}<span class="rounded bg-red-100 px-1.5 text-xs text-red-800">REVOKED</span>{/if}
				<button class="ml-auto text-xs text-neutral-500 hover:text-red-600" onclick={() => app.removeContact(c.name)}>remove</button>
			</li>
		{:else}
			<li class="text-neutral-500">No contacts yet.</li>
		{/each}
	</ul>
	<details class="mt-2 text-sm">
		<summary class="cursor-pointer text-neutral-600">Add a contact by key (without ENS)</summary>
		<div class="mt-2 flex flex-col gap-2">
			<input class="input" bind:value={newName} placeholder="handheld.bob.kinjo.eth" />
			<input class="input" bind:value={newPub} placeholder="0x… public key" />
			<button class="btn self-start" onclick={addContact}>Add</button>
		</div>
	</details>

	{#if error}<p class="mt-3 text-sm text-red-600">{error}</p>{/if}
	{#if app.notices.length}
		<ul class="mt-3 space-y-0.5 rounded bg-neutral-100 p-2 text-xs">
			{#each app.notices as n, i (i)}<li>{n}</li>{/each}
		</ul>
	{/if}

	<h3 class="mt-5 font-medium">Packets seen by the bridge</h3>
	<p class="text-xs text-neutral-500">What any relay sees: headers and ciphertext.</p>
	<div class="mt-2 max-h-48 overflow-auto font-mono text-xs">
		{#each app.packets as p, i (i)}
			<div class:text-neutral-400={!p.forMe}>
				{time(p.at)} {p.fromMac} id={p.header.messageId.toString(16).padStart(8, '0')}
				{p.header.source.toString(16).padStart(8, '0')}→{p.header.destination.toString(16).padStart(8, '0')}
				ttl={p.header.ttl} {p.header.fragIndex + 1}/{p.header.fragCount} {p.size}B {p.preview}…
			</div>
		{/each}
	</div>
	{#if app.bridgeLog.length}
		<pre class="mt-3 max-h-24 overflow-auto rounded bg-neutral-100 p-2 text-xs">{app.bridgeLog.join('\n')}</pre>
	{/if}
</Card>
