<script lang="ts">
	import { app } from '$lib/kinjo/state.svelte';
</script>

{#if app.portChoice}
	<div class="fixed inset-0 z-10 flex items-center justify-center bg-black/30">
		<div class="w-full max-w-md rounded-lg bg-white p-4 shadow-lg">
			<h2 class="mb-2 font-semibold">Pick a serial port</h2>
			<ul class="space-y-1">
				{#each app.portChoice.ports as p (p.path)}
					<li>
						<button class="w-full rounded border border-neutral-200 p-2 text-left text-sm hover:bg-neutral-100" onclick={() => app.portChoice?.resolve(p.path)}>
							<b>{p.path}</b> · {p.description}
							{#if p.serial}<span class="block text-xs text-neutral-500">{p.serial}</span>{/if}
						</button>
					</li>
				{:else}
					<li class="text-sm text-neutral-500">No boards found.</li>
				{/each}
			</ul>
			<button class="btn-secondary mt-3" onclick={() => app.portChoice?.resolve(null)}>Cancel</button>
		</div>
	</div>
{/if}
