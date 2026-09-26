<script lang="ts">
	import { goto } from '$app/navigation';

	import Card from './Card.svelte';
	import { ensConfig } from '$lib/kinjo/config';
	import { LABEL_PATTERN } from '$lib/kinjo/onboarding';
	import { app } from '$lib/kinjo/state.svelte';

	let error = $state('');
	let label = $state('');
	let step = $state<'start' | 'claim' | 'done'>('start');
	let info = $state('');
	const labelOk = $derived(LABEL_PATTERN.test(label) && label !== 'verified');
	const sponsored = !!ensConfig.relayerUrl;

	async function run(action: () => Promise<unknown>) {
		error = '';
		try {
			await action();
		} catch (e) {
			error = (e as Error).message.split('\n')[0];
		}
	}

	const enter = () => goto(app.name ? '/chat' : '/devices');

	/** Sign in: the wallet must already have a name. */
	const signIn = () =>
		run(async () => {
			info = '';
			await app.connectWallet();
			if (app.signedIn) enter();
			else info = "This wallet has no Kinjo name yet. Register to claim one.";
		});

	/** Register: claim a name. A wallet that already has one is simply signed in. */
	const register = () =>
		run(async () => {
			info = '';
			await app.connectWallet();
			if (app.signedIn) enter();
			else step = 'claim';
		});

	const claim = () =>
		run(async () => {
			await app.signUp(label);
			step = 'done';
		});
</script>

<div class="mx-auto max-w-lg space-y-6 sm:pt-10">
	<Card title="Welcome">
		<p class="text-center text-sm">
			Kinjo is a chat for your neighborhood that works without internet. Small radio devices carry encrypted messages and
			drawings from device to device. Who is who comes from ENS: your name, your devices and their keys.
		</p>
	</Card>

	{#if step === 'start'}
		<Card title="Sign in or register">
			<div class="text-center">
				<p class="text-sm text-frame-dark">
					Your Kinjo name lives in ENS under <b>kinjo.eth</b> and belongs to your wallet. Registering is free.
					{sponsored ? 'You only sign messages, no Sepolia ETH needed.' : 'You pay the gas yourself.'}
				</p>
				<div class="mt-4 flex flex-wrap justify-center gap-3">
					<button class="btn px-6" disabled={!app.online || !!app.busy} onclick={signIn}>Sign in</button>
					<button class="btn-secondary px-6" disabled={!app.online || !!app.busy} onclick={register}>Register</button>
				</div>
				{#if info}<p class="mt-3 text-sm text-amber-800">{info}</p>{/if}
				{#if !app.online}<p class="mt-2 text-xs text-amber-700">Signing in needs internet once. After that the chat works offline.</p>{/if}
			</div>
		</Card>
	{:else if step === 'claim'}
		<Card title="Register: claim your name">
			<label class="block text-sm text-frame-dark" for="label">Your name</label>
			<div class="mt-1 flex items-center gap-1">
				<input id="label" class="input flex-1" bind:value={label} placeholder="alice" />
				<span class="text-sm text-frame-dark">.kinjo.eth</span>
			</div>
			<p class="mt-1 text-xs text-frame-dark">
				Lowercase a-z, 0-9 and "-". This laptop becomes your first device: <b>laptop.{labelOk ? label : '…'}.kinjo.eth</b>.
			</p>
			<div class="mt-4 flex justify-center gap-3">
				<button class="btn-secondary" onclick={() => (step = 'start')}>Back</button>
				<button class="btn px-6" disabled={!labelOk || !!app.busy} onclick={claim}>Claim name</button>
			</div>
		</Card>
	{:else}
		<Card title="You're in">
			<p class="text-sm">
				<b>{app.owner?.label}.kinjo.eth</b> is yours. This laptop is <b>{app.name}</b>.
			</p>
			<div class="mt-3 rounded-sm border-2 border-frame bg-console p-3 text-sm">
				<b>Optional: verify you're human.</b>
				<p class="mt-1 text-frame-dark">
					A World ID badge (<i>{app.owner?.label}.verified.kinjo.eth</i>) lets others filter for real people. You can do it
					later in Settings.
				</p>
				<a href="/settings" class="btn-secondary mt-2 inline-block">Verify in Settings</a>
			</div>
			<div class="mt-4 text-center"><a href="/devices" class="btn inline-block px-6">Next: add your handheld</a></div>
		</Card>
	{/if}

	{#if app.busy}<p class="text-center text-sm text-blue-700">{app.busy}…</p>{/if}
	{#if error}<p class="text-center text-sm text-red-600">{error}</p>{/if}
</div>
