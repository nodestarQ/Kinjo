<script lang="ts">
	import DevicePanel from '$lib/components/DevicePanel.svelte';
	import IdentityPanel from '$lib/components/IdentityPanel.svelte';
	import MeshPanel from '$lib/components/MeshPanel.svelte';
	import OwnerPanel from '$lib/components/OwnerPanel.svelte';
	import { serialSupported } from '$lib/kinjo/serial';
</script>

<main class="mx-auto max-w-6xl p-6">
	<h1 class="text-2xl font-semibold">Kinjo</h1>
	<p class="text-neutral-600">A neighborhood mesh with trust from ENS.</p>

	{#if !serialSupported()}
		<p class="mt-4 rounded bg-amber-100 p-3 text-sm text-amber-900">
			This browser can't talk to USB devices. Use Chrome or Edge.
		</p>
	{/if}

	<div class="mt-6 grid gap-6 lg:grid-cols-2">
		<div class="space-y-6">
			<OwnerPanel />
			<DevicePanel />
			<IdentityPanel />
		</div>
		<MeshPanel />
	</div>
</main>
