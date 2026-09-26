// Serial link to a Kinjo board (SPEC §12). Uses the browser's Web Serial (Chrome, Edge), or the
// local serial helper (firmware/tools/serial_helper.py) where Web Serial doesn't work.

import { FrameReader, serialFrame } from './protocol';

// Minimal Web Serial types (not in TypeScript's DOM lib yet).
interface SerialPortLike {
	open(options: { baudRate: number }): Promise<void>;
	setSignals(signals: { dataTerminalReady?: boolean; requestToSend?: boolean }): Promise<void>;
	close(): Promise<void>;
	readable: ReadableStream<Uint8Array> | null;
	writable: WritableStream<Uint8Array> | null;
	getInfo(): { usbVendorId?: number; usbProductId?: number };
}
interface SerialLike {
	requestPort(options?: object): Promise<SerialPortLike>;
}

export type FrameHandler = (type: number, body: Uint8Array) => void;

export interface HelperPort {
	path: string;
	description: string;
	serial: string;
}

/** Lets the user pick one of the helper's ports. Resolves null if cancelled. */
export type PortChooser = (ports: HelperPort[]) => Promise<string | null>;

export function serialSupported(helperUrl = ''): boolean {
	return !!helperUrl || (typeof navigator !== 'undefined' && 'serial' in navigator);
}

function helperPorts(url: string): Promise<HelperPort[]> {
	return new Promise((resolve, reject) => {
		const ws = new WebSocket(`${url}/ports`);
		ws.onmessage = (e) => resolve(JSON.parse(e.data as string));
		ws.onerror = () => reject(new Error(`serial helper not reachable at ${url}`));
	});
}

export class SerialLink {
	private port: SerialPortLike | null = null;
	private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
	private closing = false;

	private ws: WebSocket | null = null;

	constructor(
		private onFrame: FrameHandler,
		private onClose: () => void = () => {},
		private helperUrl = '',
		private choosePort: PortChooser = async () => null
	) {}

	get connected(): boolean {
		return this.port !== null || this.ws !== null;
	}

	/** Asks the user to pick a port, opens it and starts reading. Opening resets a XIAO. */
	async connect(): Promise<void> {
		if (this.helperUrl) return this.connectHelper();
		const serial = (navigator as unknown as { serial: SerialLike }).serial;
		this.port = await serial.requestPort();
		console.info('[kinjo serial] opening', this.port.getInfo());
		await this.port.open({ baudRate: 115200 });
		console.info('[kinjo serial] open, readable:', !!this.port.readable, 'writable:', !!this.port.writable);
		this.closing = false;
		void this.readLoop();
	}

	private async connectHelper(): Promise<void> {
		const path = await this.choosePort(await helperPorts(this.helperUrl));
		if (!path) throw new Error('no port picked');
		const frames = new FrameReader();
		const ws = new WebSocket(`${this.helperUrl}/open?path=${encodeURIComponent(path)}`);
		ws.binaryType = 'arraybuffer';
		await new Promise<void>((resolve, reject) => {
			ws.onopen = () => resolve();
			ws.onerror = () => reject(new Error(`serial helper could not open ${path}`));
		});
		this.ws = ws;
		this.closing = false;
		ws.onmessage = (e) => {
			for (const f of frames.push(new Uint8Array(e.data as ArrayBuffer))) this.onFrame(f.type, f.body);
		};
		ws.onclose = () => {
			this.ws = null;
			if (!this.closing) this.onClose();
		};
	}

	async send(type: number, body: Uint8Array): Promise<void> {
		if (this.ws) {
			this.ws.send(serialFrame(type, body) as Uint8Array<ArrayBuffer>);
			return;
		}
		if (!this.port?.writable) {
			console.warn('[kinjo serial] send without an open port', { port: !!this.port, closing: this.closing });
			throw new Error('not connected');
		}
		const writer = this.port.writable.getWriter();
		try {
			await writer.write(serialFrame(type, body));
		} finally {
			writer.releaseLock();
		}
	}

	async disconnect(): Promise<void> {
		this.closing = true;
		if (this.ws) {
			this.ws.close();
			this.ws = null;
			this.onClose();
			return;
		}
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
					console.debug('[kinjo serial] read', value.length, 'bytes');
					for (const f of frames.push(value)) this.onFrame(f.type, f.body);
				}
			} catch (e) {
				console.warn('[kinjo serial] read error', (e as Error).name, (e as Error).message, 'readable after:', !!this.port?.readable);
				// Framing, break and overrun errors (e.g. the ESP32 boot text at another baud rate)
				// are recoverable: the browser offers a fresh stream. Unplugging leaves none.
				if (!this.port?.readable) break;
			} finally {
				this.reader.releaseLock();
			}
		}
		if (!this.closing) {
			console.warn('[kinjo serial] port lost');
			await this.port?.close().catch(() => {}); // release it, or the browser keeps it locked
			this.port = null;
			this.onClose();
		}
	}
}
