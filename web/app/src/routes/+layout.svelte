<script lang="ts">
	import '../app.css';
	import logo from '$lib/assets/logo.svg';

	import NavBar from '$lib/components/NavBar.svelte';
	import PortPicker from '$lib/components/PortPicker.svelte';
	import { serialHelperUrl } from '$lib/kinjo/config';
	import { serialSupported } from '$lib/kinjo/serial';

	let { children } = $props();
</script>

<svelte:head>
	<link rel="icon" type="image/svg+xml" href={logo} />
	<link rel="manifest" href="/manifest.webmanifest" />
	<meta name="theme-color" content="#8ea0b3" />
	<title>Kinjo</title>
</svelte:head>

<div class="min-h-screen">
	<main class="mx-auto max-w-6xl space-y-4 p-3 sm:space-y-6 sm:p-6">
		<NavBar />
		{#if !serialSupported(serialHelperUrl)}
			<p class="rounded bg-amber-100 p-3 text-sm text-amber-900">This browser can't talk to USB devices. Use Chrome or Edge.</p>
		{/if}
		{@render children()}
	</main>
	<PortPicker />
</div>
