<script lang="ts">
	import { PALETTE, type ColoredStroke } from '$lib/kinjo/protocol';

	let { strokes }: { strokes: ColoredStroke[] } = $props();
	const id = $props.id();
</script>

<!-- Ruled like the handheld's draw screen: a line every 12 px on the 320 x 240 canvas. -->
<svg viewBox="0 0 320 240" class="w-full max-w-xs">
	<defs>
		<pattern id="rules-{id}" width="320" height="12" patternUnits="userSpaceOnUse">
			<line x1="0" y1="11.5" x2="320" y2="11.5" stroke="#d7e2ec" stroke-width="1" />
		</pattern>
	</defs>
	<rect width="320" height="240" fill="url(#rules-{id})" />
	{#each strokes as s, i (i)}
		{#if s.points.length === 1}
			<circle cx={s.points[0][0]} cy={s.points[0][1]} r="1.5" fill={PALETTE[s.color]} />
		{:else}
			<polyline points={s.points.map((p) => p.join(',')).join(' ')} fill="none" stroke={PALETTE[s.color]} stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" />
		{/if}
	{/each}
</svg>
