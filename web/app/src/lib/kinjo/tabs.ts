// Every name gets the same tab color everywhere, like players in a chat room.

const COLORS = ['#3d6fd6', '#2f9e5b', '#e0832f', '#d4548e', '#8a57d6', '#1f9aa8', '#c9a227'];

export function tabColor(name: string): string {
	let h = 0;
	for (const c of name) h = (h * 31 + c.charCodeAt(0)) >>> 0;
	return COLORS[h % COLORS.length];
}

/** "handheld.alice.kinjo.eth" → "handheld.alice" */
export const shortName = (name: string) => name.replace(/\.kinjo\.eth$/, '');
