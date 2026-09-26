<script lang="ts">
	import { LABEL_PATTERN } from '$lib/kinjo/onboarding';
	import { app } from '$lib/kinjo/state.svelte';
	import { shortName, tabColor } from '$lib/kinjo/tabs';

	let { name, note = '', run }: { name: string; note?: string; run: (action: () => Promise<unknown>) => void } = $props();

	let renaming = $state(false);
	let newLabel = $state('');
	const same = $derived(newLabel === name.split('.')[0]);
	const ok = $derived(LABEL_PATTERN.test(newLabel) && !same);
	const rename = () => ok && !locked && run(async () => ((await app.renameDevice(name, newLabel)), (renaming = false)));
	const locked = $derived(!app.hasWallet || !app.online || !!app.busy);
</script>

<li class="rounded-sm border-2 border-rule p-2">
	<div class="flex flex-wrap items-center gap-2">
		<span class="tab rounded-sm" style="background:{tabColor(name)}">{shortName(name)}</span>
		{#if note}<span class="text-xs text-frame-dark">{note}</span>{/if}
		<span class="ml-auto flex gap-2">
			<button class="btn-secondary" disabled={locked} onclick={() => ((renaming = !renaming), (newLabel = ''))}>Rename</button>
			<button
				class="btn-danger"
				disabled={locked}
				onclick={() => confirm(`Revoke ${name}? Others will reject its messages.`) && run(() => app.revoke(name.split('.')[0]))}
			>
				Revoke
			</button>
		</span>
	</div>
	{#if renaming}
		<div class="mt-2 flex flex-wrap items-center gap-1">
			<input
				class="input w-32"
				value={newLabel}
				oninput={(e) => (newLabel = e.currentTarget.value = e.currentTarget.value.toLowerCase())}
				onkeydown={(e) => e.key === 'Enter' && rename()}
				placeholder="new name"
			/>
			<span class="text-sm text-frame-dark">.{app.owner?.label}.kinjo.eth</span>
			<button
				class="btn w-full sm:ml-auto sm:w-auto"
				disabled={!ok || locked}
				onclick={rename}
			>
				Rename (2 signatures)
			</button>
		</div>
		{#if newLabel && !ok}
			<p class="mt-1 text-xs text-frame-dark">{same ? 'That is the current name.' : 'Use a-z, 0-9 and "-" only.'}</p>
		{/if}
	{/if}
</li>
