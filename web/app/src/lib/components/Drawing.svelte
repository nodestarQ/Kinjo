<script lang="ts">
	import type { Stroke } from '$lib/kinjo/protocol';

	let { strokes }: { strokes: Stroke[] } = $props();
</script>

<svg viewBox="0 0 320 240" class="w-full max-w-xs rounded border border-neutral-300 bg-white">
	{#each strokes as stroke, i (i)}
		{#if stroke.length === 1}
			<circle cx={stroke[0][0]} cy={stroke[0][1]} r="1.5" fill="black" />
		{:else}
			<polyline points={stroke.map((p) => p.join(',')).join(' ')} fill="none" stroke="black" stroke-width="2" />
		{/if}
	{/each}
</svg>
