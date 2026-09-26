// Every device gets the same tab color everywhere (web app and handheld, see firmware/src/handheld/ui.h).
// FNV-1a spreads names that share the ".kinjo.eth" ending well across the 12 colors.

export const TAB_COLORS = [
	'#3d6fd6', '#2f9e5b', '#e0832f', '#d4548e', '#8a57d6', '#1f9aa8',
	'#c9a227', '#d8453b', '#5b8c2a', '#2b4fa0', '#b0569e', '#7a5c3e'
];

export function tabColor(name: string): string {
	let h = 0x811c9dc5;
	for (const b of new TextEncoder().encode(name)) h = Math.imul(h ^ b, 0x01000193) >>> 0;
	return TAB_COLORS[h % TAB_COLORS.length];
}

/** "handheld.alice.kinjo.eth" → "handheld.alice" */
export const shortName = (name: string) => name.replace(/\.kinjo\.eth$/, '');
