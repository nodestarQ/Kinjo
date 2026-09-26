<script lang="ts">
	import type { Stroke } from '$lib/kinjo/protocol';

	let { strokes }: { strokes: Stroke[] } = $props();
</script>

<svg viewBox="0 0 320 240" class="w-full max-w-xs rounded-sm border border-rule bg-white">
	{#each strokes as stroke, i (i)}
		{#if stroke.length === 1}
			<circle cx={stroke[0][0]} cy={stroke[0][1]} r="1.5" fill="#26313d" />
		{:else}
			<polyline points={stroke.map((p) => p.join(',')).join(' ')} fill="none" stroke="#26313d" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" />
		{/if}
	{/each}
</svg>
