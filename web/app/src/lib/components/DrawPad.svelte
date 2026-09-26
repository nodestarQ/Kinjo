<script lang="ts">
	import { CANVAS_H, CANVAS_W, PALETTE, type ColoredStroke } from '$lib/kinjo/protocol';

	// The drawing part of the note. The composer sends it together with the text.
	let { strokes = $bindable([]) }: { strokes: ColoredStroke[] } = $props();

	let canvas: HTMLCanvasElement | undefined = $state();
	let color = $state(0);
	let drawing = false;

	const RULE = 12; // same ruling as the handheld's draw screen

	function paint() {
		const ctx = canvas?.getContext('2d');
		if (!ctx) return;
		ctx.fillStyle = '#fcfdfe';
		ctx.fillRect(0, 0, CANVAS_W, CANVAS_H);
		ctx.strokeStyle = '#d7e2ec';
		ctx.lineWidth = 1;
		for (let y = RULE - 0.5; y < CANVAS_H; y += RULE) {
			ctx.beginPath();
			ctx.moveTo(0, y);
			ctx.lineTo(CANVAS_W, y);
			ctx.stroke();
		}
		ctx.lineWidth = 2.5;
		ctx.lineCap = ctx.lineJoin = 'round';
		for (const s of strokes) {
			ctx.strokeStyle = ctx.fillStyle = PALETTE[s.color];
			ctx.beginPath();
			ctx.moveTo(s.points[0][0], s.points[0][1]);
			for (const [x, y] of s.points.slice(1)) ctx.lineTo(x, y);
			if (s.points.length === 1) ctx.arc(s.points[0][0], s.points[0][1], 1.5, 0, Math.PI * 2);
			ctx.stroke();
		}
	}

	$effect(() => {
		void strokes.length;
		paint();
	});

	/** Pointer position in canvas pixels (the canvas is shown scaled). */
	function point(e: PointerEvent): [number, number] {
		const r = canvas!.getBoundingClientRect();
		const x = Math.floor(((e.clientX - r.left) / r.width) * CANVAS_W);
		const y = Math.floor(((e.clientY - r.top) / r.height) * CANVAS_H);
		return [Math.min(CANVAS_W - 1, Math.max(0, x)), Math.min(CANVAS_H - 1, Math.max(0, y))];
	}

	function down(e: PointerEvent) {
		canvas!.setPointerCapture(e.pointerId);
		drawing = true;
		strokes.push({ color, points: [point(e)] });
	}

	function move(e: PointerEvent) {
		if (!drawing) return;
		const s = strokes[strokes.length - 1];
		const [x, y] = point(e);
		const [lx, ly] = s.points[s.points.length - 1];
		if ((x - lx) ** 2 + (y - ly) ** 2 < 9) return; // same jitter filter as the handheld
		s.points.push([x, y]);
		paint();
	}

</script>

<div class="flex flex-col gap-2">
	<canvas
		bind:this={canvas}
		width={CANVAS_W}
		height={CANVAS_H}
		class="w-full max-w-sm cursor-crosshair touch-none rounded-sm border-2 border-frame"
		onpointerdown={down}
		onpointermove={move}
		onpointerup={() => (drawing = false)}
		onpointercancel={() => (drawing = false)}
	></canvas>
	<div class="flex flex-wrap items-center gap-1">
		{#each PALETTE as c, i (c)}
			<button
				class="h-7 w-7 rounded-sm border-2"
				class:border-frame-dark={i !== color}
				class:border-white={i === color}
				class:ring-2={i === color}
				style="background:{c}; --tw-ring-color:{c}"
				aria-label="pen color {i}"
				onclick={() => (color = i)}
			></button>
		{/each}
		<button class="btn-secondary ml-2" onclick={() => (strokes = [])}>Clear</button>
	</div>
</div>
