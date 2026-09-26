<script lang="ts">
	import type { Snippet } from 'svelte';

	let { title, children }: { title: string; children: Snippet } = $props();
</script>

<section class="panel overflow-hidden">
	<h2 class="panel-title"><span aria-hidden="true">▶</span>{title}</h2>
	<div class="p-4">
		{@render children()}
	</div>
</section>
