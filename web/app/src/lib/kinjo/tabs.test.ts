import { describe, expect, it } from 'vitest';

import { TAB_COLORS, tabColor } from './tabs';

describe('tabColor', () => {
	it('gives devices of the same owner different colors', () => {
		expect(tabColor('laptop.yippie.kinjo.eth')).not.toBe(tabColor('handheld.yippie.kinjo.eth'));
		expect(tabColor('laptop.alice.kinjo.eth')).not.toBe(tabColor('handheld.alice.kinjo.eth'));
	});

	it('matches FNV-1a mod 12, as on the handheld', () => {
		// Values computed with Python: fnv1a(name) % 12
		expect(tabColor('laptop.yippie.kinjo.eth')).toBe(TAB_COLORS[7]);
		expect(tabColor('handheld.yippie.kinjo.eth')).toBe(TAB_COLORS[5]);
		expect(tabColor('handheld.bob.kinjo.eth')).toBe(TAB_COLORS[8]);
	});
});
