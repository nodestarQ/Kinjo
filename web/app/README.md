# Kinjo web app

Onboarding (ENS names, device setup over USB) and the laptop node (talks to the radio bridge over Web Serial). Runs only in the browser: Chrome or Edge, served as static files.

```sh
cd web/app
pnpm install
pnpm dev      # http://localhost:5173
pnpm test     # protocol tests against protocol/test-vectors
pnpm build    # static files in build/
```

| Folder | What |
|---|---|
| `src/lib/kinjo/protocol.ts` | wire format (SPEC.md): packets, crypto, serial frames, provisioning |
| `src/routes` | pages |
