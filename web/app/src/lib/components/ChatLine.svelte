<script lang="ts">
	import Drawing from './Drawing.svelte';
	import type { Message } from '$lib/kinjo/mesh';
	import { shortName, tabColor } from '$lib/kinjo/tabs';

	let { m, me }: { m: Message; me: string } = $props();

	// Outgoing: our own tab. Incoming: the sender's tab.
	const who = $derived(m.outgoing ? me || 'this laptop' : m.from);
	const color = $derived(m.status === 'verified' ? tabColor(who) : '#9aa5b1');
	const rejected = $derived(!m.outgoing && m.status !== 'verified');
</script>

<!-- Own messages on the left, other devices on the right. -->
<li class="flex flex-col" class:items-start={m.outgoing} class:items-end={!m.outgoing}>
	<span class="tab px-2 py-0.5 {m.outgoing ? 'rounded-l-none rounded-tl-sm' : 'rounded-tr-sm rounded-l-none rounded-tl-sm'}" style="background:{color}">
		{shortName(who)}{#if !m.outgoing && m.status === 'verified'}<span class="ml-1" title="key matches ENS">✓</span>{/if}
	</span>
	<div
		class="min-h-9 w-[85%] rounded-sm border-2 bg-paper px-2 py-1.5 {m.outgoing ? 'rounded-tl-none' : 'rounded-tr-none'}"
		style="border-color:{color}"
	>
		{#if rejected}
			<span class="text-sm text-red-700">{m.status === 'revoked' ? 'REVOKED in ENS' : m.status}: message rejected</span>
		{:else}
			{#if m.text !== undefined}<span>{m.text}</span>{/if}
			{#if m.strokes}<div class="mt-1 max-w-60"><Drawing strokes={m.strokes} /></div>{/if}
		{/if}
	</div>
</li>
