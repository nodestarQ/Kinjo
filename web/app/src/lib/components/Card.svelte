<script lang="ts">
	import type { Snippet } from 'svelte';

	let { title, children }: { title: string; children: Snippet } = $props();
</script>

<section class="rounded-lg border border-neutral-200 bg-white p-4 shadow-sm">
	<h2 class="mb-3 text-lg font-semibold">{title}</h2>
	{@render children()}
</section>
