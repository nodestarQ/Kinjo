<script lang="ts">
	import type { Snippet } from 'svelte';
	import { goto } from '$app/navigation';

	import { app } from '$lib/kinjo/state.svelte';

	let { children }: { children: Snippet } = $props();

	$effect(() => {
		if (!app.signedIn) goto('/', { replaceState: true });
	});
</script>

{#if app.signedIn}{@render children()}{/if}
