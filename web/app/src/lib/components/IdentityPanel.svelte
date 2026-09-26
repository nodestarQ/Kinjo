<script lang="ts">
	import Card from './Card.svelte';
	import { app } from '$lib/kinjo/state.svelte';
</script>

<!-- The name comes from ENS. Renaming happens on the Devices page (it's an ENS transaction). -->
<Card title="This laptop">
	<p class="text-sm text-frame-dark">ENS name of this laptop node</p>
	<p class="mt-1 text-sm">
		{#if app.name}<b>{app.name}</b>{:else}not registered yet{/if}
		· <a class="text-accent underline" href="/devices">{app.name ? 'Rename or revoke' : 'Register'} on Devices</a>
	</p>
	<p class="mt-3 text-sm text-frame-dark">Public key</p>
	<code class="block break-all text-xs">0x{app.pubHex}</code>
	<p class="mt-1 text-xs text-frame-dark">Node ID {app.node.id.toString(16).padStart(8, '0')}. The private key stays in this browser.</p>
	<button class="btn-secondary mt-3" onclick={() => confirm('Make a new key? The ENS record still has the old one, so revoke and register this laptop again afterwards.') && app.resetKey()}>
		New key
	</button>
</Card>
