<script lang="ts">
	import Card from './Card.svelte';
	import { app } from '$lib/kinjo/state.svelte';

	let name = $state(app.name);
</script>

<Card title="This laptop">
	<label class="block text-sm text-neutral-600" for="laptop-name">ENS name of this laptop node</label>
	<div class="mt-1 flex gap-2">
		<input id="laptop-name" class="input flex-1" bind:value={name} placeholder="laptop.alice.kinjo.eth" />
		<button class="btn" onclick={() => app.setName(name.trim())}>Save</button>
	</div>
	<p class="mt-3 text-sm text-neutral-600">Public key</p>
	<code class="block break-all text-xs">0x{app.pubHex}</code>
	<p class="mt-1 text-xs text-neutral-500">Node ID {app.node.id.toString(16).padStart(8, '0')}. The private key stays in this browser.</p>
	<button class="btn-secondary mt-3" onclick={() => confirm('Make a new key? Contacts that know the old one must be updated.') && app.resetKey()}>
		New key
	</button>
</Card>
