<script lang="ts">
	import { goto } from '$app/navigation';

	import Card from '$lib/components/Card.svelte';
	import IdentityPanel from '$lib/components/IdentityPanel.svelte';
	import RequireSignIn from '$lib/components/RequireSignIn.svelte';
	import WorldVerify from '$lib/components/WorldVerify.svelte';
	import { app } from '$lib/kinjo/state.svelte';

	let releaseLabel = $state('');
	let error = $state('');

	async function run(action: () => Promise<unknown>) {
		error = '';
		try {
			await action();
		} catch (e) {
			error = (e as Error).message.split('\n')[0];
		}
	}
</script>

<RequireSignIn>
	<div class="grid gap-6 lg:grid-cols-2">
		<div class="space-y-6">
			<Card title="Verified human (World ID)">
				<WorldVerify />
			</Card>

			<Card title="Account">
				<p class="text-sm">Signed in as <b>{app.owner?.label}.kinjo.eth</b> ({app.owner?.address.slice(0, 6)}…{app.owner?.address.slice(-4)}).</p>
				<button class="btn-secondary mt-3" onclick={() => (app.signOut(), goto('/'))}>Sign out</button>
			</Card>

			{#if app.isTeam}
				<Card title="Team: release a name">
					<p class="text-sm text-frame-dark">Frees label.kinjo.eth and its badge so it can be claimed again (demo reset).</p>
					<div class="mt-2 flex gap-2">
						<input class="input flex-1" bind:value={releaseLabel} placeholder="alice" />
						<button class="btn-danger" disabled={!!app.busy} onclick={() => run(() => app.release(releaseLabel.trim()))}>Release</button>
					</div>
					{#if error}<p class="mt-2 text-sm text-red-600">{error}</p>{/if}
				</Card>
			{/if}
		</div>
		<IdentityPanel />
	</div>
</RequireSignIn>
