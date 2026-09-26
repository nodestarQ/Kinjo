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

<li class="flex items-start">
	<span class="tab mt-0 flex min-w-20 items-center py-1" style="background:{color}">
		{shortName(who)}{#if !m.outgoing && m.status === 'verified'}<span class="ml-1" title="key matches ENS">✓</span>{/if}
	</span>
	<div class="paper min-h-11 flex-1 rounded-sm rounded-tl-none border-2 px-2 py-1 leading-[22px]" style="border-color:{color}">
		{#if m.outgoing}<span class="text-xs text-frame-dark">to {shortName(m.from)} · </span>{/if}
		{#if rejected}
			<span class="text-sm text-red-700">{m.status === 'revoked' ? 'REVOKED in ENS' : m.status}: message rejected</span>
		{:else}
			{#if m.text !== undefined}<span>{m.text}</span>{/if}
			{#if m.strokes}<div class="mt-1 max-w-60"><Drawing strokes={m.strokes} /></div>{/if}
		{/if}
	</div>
</li>
