<script lang="ts">
	import DevicePanel from '$lib/components/DevicePanel.svelte';
	import MyDevices from '$lib/components/MyDevices.svelte';
	import RequireSignIn from '$lib/components/RequireSignIn.svelte';
</script>

<RequireSignIn>
	<div class="grid gap-4 sm:gap-6 lg:grid-cols-2">
		<div class="space-y-4 sm:space-y-6">
			<MyDevices section="this" />
			<DevicePanel />
		</div>
		<MyDevices section="others" />
	</div>
</RequireSignIn>
