<script lang="ts">
	import { hexToBytes } from '@noble/hashes/utils.js';

	import BridgeBar from './BridgeBar.svelte';
	import Card from './Card.svelte';
	import { app } from '$lib/kinjo/state.svelte';
	import { shortName, tabColor } from '$lib/kinjo/tabs';
	import type { Message } from '$lib/kinjo/mesh';

	let error = $state('');
	let ensName = $state('');
	let newName = $state('');
	let newPub = $state('');

	/** Last message per chat partner. */
	const last = $derived.by(() => {
		const out: Record<string, Message> = {};
		for (const m of app.messages) out[m.from] = m;
		return out;
	});

	// Active contacts first, most recent chat on top. Revoked ones at the end.
	const contacts = $derived(
		[...app.contacts].sort(
			(a, b) => Number(!!a.revoked) - Number(!!b.revoked) || (last[b.name]?.at ?? 0) - (last[a.name]?.at ?? 0)
		)
	);
	/** Messages from nodes that aren't contacts (no key in ENS or on this laptop). */
	const strangers = $derived(
		Object.keys(last).filter((name) => !app.contacts.some((c) => c.name === name))
	);

	function preview(m: Message | undefined): string {
		if (!m) return 'No messages yet';
		const body = m.status !== 'verified' ? `${m.status}: rejected` : m.text || (m.strokes ? 'Drawing' : '');
		return (m.outgoing ? 'You: ' : '') + body;
	}

	const time = (t: number) => new Date(t).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
	const href = (name: string) => `/chat?with=${encodeURIComponent(name)}`;

	async function run(action: () => Promise<unknown>) {
		error = '';
		try {
			await action();
		} catch (e) {
			error = (e as Error).message;
		}
	}

	const addByEns = () =>
		run(async () => {
			await app.addContactByName(ensName);
			ensName = '';
		});

	const addByKey = () =>
		run(async () => {
			const pub = hexToBytes(newPub.trim().replace(/^0x/, ''));
			if (pub.length !== 32 || !newName.trim()) throw new Error('need a name and a 32-byte key');
			app.upsertContact({ name: newName.trim(), pub });
			newName = newPub = '';
		});
</script>

<Card title="Chats">
	<BridgeBar />

	<ul class="mt-4 divide-y-2 divide-rule rounded-sm border-2 border-frame bg-paper">
		{#each contacts as c (c.name)}
			{@const n = app.unread(c.name)}
			<li class="flex items-center">
				<a href={href(c.name)} class="flex min-w-0 flex-1 items-center gap-3 px-3 py-2 hover:bg-console">
					<span class="h-8 w-2 shrink-0 rounded-sm" style="background:{c.revoked ? '#9aa5b1' : tabColor(c.name)}"></span>
					<span class="min-w-0 flex-1">
						<span class="flex items-center gap-2">
							<span class="truncate font-medium" class:line-through={c.revoked}>{shortName(c.name)}</span>
							{#if c.verifiedHuman}<span class="rounded bg-blue-100 px-1.5 text-xs text-blue-800">human</span>{/if}
							{#if c.revoked}<span class="rounded bg-red-100 px-1.5 text-xs text-red-800">REVOKED</span>{/if}
						</span>
						<span class="block truncate text-sm text-frame-dark">{preview(last[c.name])}</span>
					</span>
					<span class="flex shrink-0 flex-col items-end gap-1 text-xs text-frame-dark">
						{#if last[c.name]}{time(last[c.name].at)}{/if}
						{#if n}<span class="rounded-full bg-red-600 px-1.5 text-white">{n}</span>{/if}
					</span>
				</a>
				<button
					class="px-3 text-xs text-frame-dark hover:text-red-600"
					title="remove from this laptop"
					onclick={() => confirm(`Remove ${shortName(c.name)} from your contacts?`) && app.removeContact(c.name)}>✕</button
				>
			</li>
		{:else}
			<li class="px-3 py-6 text-center text-sm text-frame-dark">No contacts yet. Add one below.</li>
		{/each}
		{#each strangers as s (s)}
			<li>
				<a href={href(s)} class="flex items-center gap-3 px-3 py-2 hover:bg-console">
					<span class="h-8 w-2 shrink-0 rounded-sm bg-[#9aa5b1]"></span>
					<span class="min-w-0 flex-1">
						<span class="block font-medium">Unknown node {s}</span>
						<span class="block truncate text-sm text-frame-dark">{preview(last[s])}</span>
					</span>
				</a>
			</li>
		{/each}
	</ul>

	<div class="mt-5 flex items-center">
		<h3 class="font-medium">Add a contact</h3>
		<button class="btn-secondary ml-auto" onclick={() => run(() => app.syncEns())}>Sync ENS</button>
	</div>
	<p class="text-xs text-frame-dark">Keys come from ENS and are checked again every 10 s.</p>
	<div class="mt-2 flex gap-2">
		<input class="input min-w-0 flex-1" bind:value={ensName} placeholder="handheld.bob.kinjo.eth" onkeydown={(e) => e.key === 'Enter' && addByEns()} />
		<button class="btn" onclick={addByEns}>Add</button>
	</div>
	<details class="mt-2 text-sm">
		<summary class="cursor-pointer text-frame-dark">Add by key (without ENS)</summary>
		<div class="mt-2 flex flex-col gap-2">
			<input class="input" bind:value={newName} placeholder="handheld.bob.kinjo.eth" />
			<input class="input" bind:value={newPub} placeholder="0x… public key" />
			<button class="btn self-start" onclick={addByKey}>Add</button>
		</div>
	</details>
	{#if error}<p class="mt-3 text-sm text-red-600">{error}</p>{/if}

	<details class="mt-5 text-sm">
		<summary class="cursor-pointer text-frame-dark">Packets seen by the bridge</summary>
		<p class="mt-1 text-xs text-frame-dark">What any relay sees: headers and ciphertext.</p>
		<div class="mt-2 max-h-48 overflow-auto font-mono text-xs">
			{#each app.packets as p, i (i)}
				<div class:text-frame={!p.forMe}>
					{new Date(p.at).toLocaleTimeString()} {p.fromMac} id={p.header.messageId.toString(16).padStart(8, '0')}
					{p.header.source.toString(16).padStart(8, '0')}→{p.header.destination.toString(16).padStart(8, '0')}
					ttl={p.header.ttl} {p.header.fragIndex + 1}/{p.header.fragCount} {p.size}B {p.preview}…
				</div>
			{:else}
				<div class="text-frame-dark">Nothing yet.</div>
			{/each}
		</div>
	</details>
</Card>
