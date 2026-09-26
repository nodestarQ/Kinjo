// Web Serial link to a Kinjo board (SPEC §12). Chrome and Edge only.

import { FrameReader, serialFrame } from './protocol';

// Minimal Web Serial types (not in TypeScript's DOM lib yet).
interface SerialPortLike {
	open(options: { baudRate: number }): Promise<void>;
	close(): Promise<void>;
	readable: ReadableStream<Uint8Array> | null;
	writable: WritableStream<Uint8Array> | null;
	getInfo(): { usbVendorId?: number; usbProductId?: number };
}
interface SerialLike {
	requestPort(options?: object): Promise<SerialPortLike>;
}

export type FrameHandler = (type: number, body: Uint8Array) => void;

export function serialSupported(): boolean {
	return typeof navigator !== 'undefined' && 'serial' in navigator;
}

export class SerialLink {
	private port: SerialPortLike | null = null;
	private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
	private closing = false;

	constructor(private onFrame: FrameHandler, private onClose: () => void = () => {}) {}

	get connected(): boolean {
		return this.port !== null;
	}

	/** Asks the user to pick a port, opens it and starts reading. Opening resets a XIAO. */
	async connect(): Promise<void> {
		const serial = (navigator as unknown as { serial: SerialLike }).serial;
		this.port = await serial.requestPort();
		await this.port.open({ baudRate: 115200 });
		this.closing = false;
		void this.readLoop();
	}

	async send(type: number, body: Uint8Array): Promise<void> {
		if (!this.port?.writable) throw new Error('not connected');
		const writer = this.port.writable.getWriter();
		try {
			await writer.write(serialFrame(type, body));
		} finally {
			writer.releaseLock();
		}
	}

	async disconnect(): Promise<void> {
		this.closing = true;
		await this.reader?.cancel().catch(() => {});
		await this.port?.close().catch(() => {});
		this.port = null;
		this.onClose();
	}

	private async readLoop(): Promise<void> {
		const frames = new FrameReader();
		while (this.port?.readable && !this.closing) {
			this.reader = this.port.readable.getReader();
			try {
				for (;;) {
					const { value, done } = await this.reader.read();
					if (done) break;
					for (const f of frames.push(value)) this.onFrame(f.type, f.body);
				}
			} catch {
				break; // unplugged
			} finally {
				this.reader.releaseLock();
			}
		}
		if (!this.closing) {
			this.port = null;
			this.onClose();
		}
	}
}
