<script lang="ts">
	import { onMount } from 'svelte';
	import { goto } from '$app/navigation';

	import Welcome from '$lib/components/Welcome.svelte';
	import { app } from '$lib/kinjo/state.svelte';

	// Already signed in when the app opens (also offline): straight to the chat.
	onMount(() => {
		if (app.signedIn && app.name) goto('/chat', { replaceState: true });
	});
</script>

<Welcome />
