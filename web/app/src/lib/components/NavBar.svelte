<script lang="ts">
	import { page } from '$app/state';

	import { app } from '$lib/kinjo/state.svelte';
	import logo from '$lib/assets/logo.svg';
	import { shortName, tabColor } from '$lib/kinjo/tabs';

	const links = [
		{ href: '/chat', label: 'Chat' },
		{ href: '/devices', label: 'Devices' },
		{ href: '/settings', label: 'Settings' }
	];
	const owner = $derived(app.owner?.label ? `${app.owner.label}.kinjo.eth` : '');
</script>

<header class="panel flex flex-wrap items-center gap-x-4 gap-y-2 px-4 py-2">
	<a href={app.signedIn ? '/chat' : '/'} class="flex items-center gap-2 text-2xl tracking-wider text-accent">
		<img src={logo} alt="" class="h-8 w-8" />Kinjo
	</a>
	{#if app.signedIn}
		<nav class="flex gap-1">
			{#each links as l (l.href)}
				<a
					href={l.href}
					class="rounded-sm border-2 px-3 py-0.5 text-sm {page.url.pathname === l.href
						? 'border-frame-dark bg-frame text-white'
						: 'border-frame bg-white hover:bg-console'}">{l.label}</a
				>
			{/each}
		</nav>
		<span class="tab ml-auto rounded-sm" style="background:{tabColor(owner)}">{shortName(owner)}</span>
	{:else}
		<span class="text-sm text-frame-dark">近所 · chat with your neighbors, no internet needed, trust from ENS</span>
	{/if}
	<span class="flex items-center gap-1 text-xs text-frame-dark" class:ml-auto={!app.signedIn} title={app.online ? 'internet available' : 'offline: mesh chat works, ENS actions wait'}>
		<span class="h-2 w-2 rounded-full {app.online ? 'bg-green-500' : 'bg-amber-500'}"></span>{app.online ? 'online' : 'offline'}
	</span>
</header>
