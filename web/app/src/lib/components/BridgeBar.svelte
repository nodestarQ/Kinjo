<script lang="ts">
	import { app } from '$lib/kinjo/state.svelte';

	let error = $state('');

	async function connect() {
		error = '';
		try {
			await app.connectBridge();
		} catch (e) {
			error = (e as Error).message;
		}
	}
</script>

<!-- Radio bridge status. The chat needs it to send and receive. -->
<div class="flex flex-wrap items-center gap-2 text-sm">
	{#if app.bridgeConnected}
		<span class="h-2 w-2 rounded-full bg-green-500"></span> Bridge connected
		<button class="btn-secondary ml-auto" onclick={() => app.disconnectBridge()}>Disconnect</button>
	{:else}
		<span class="min-w-0 flex-1 text-frame-dark">
			<span class="mr-1 inline-block h-2 w-2 rounded-full bg-amber-500"></span>Plug in the radio bridge XIAO to send and receive.
		</span>
		<button class="btn ml-auto" onclick={connect}>Connect bridge</button>
	{/if}
</div>
{#if error}<p class="mt-1 text-sm text-red-600">{error}</p>{/if}
