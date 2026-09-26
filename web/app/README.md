# Kinjo web app

Onboarding (ENS names, device setup over USB) and the laptop node (talks to the radio bridge over Web Serial). Runs only in the browser: Chrome or Edge, served as static files.

```sh
cd web/app
cp .env.example .env   # set the contract and relayer addresses
pnpm install
pnpm dev      # http://localhost:5173
pnpm test     # protocol tests; ENS tests too if the local fork runs (see below)
pnpm build    # static files in build/
```

| Folder | What |
|---|---|
| `src/lib/kinjo/protocol.ts` | wire format (SPEC.md): packets, crypto, serial frames, provisioning |
| `src/lib/kinjo/ens.ts` | device keys and badges from ENS, sync (revoked, new key), wallet, signing, relayer |
| `src/lib/kinjo/onboarding.ts` | contract ABI and EIP-712 requests, shared with `web/relayer` |
| `src/lib/kinjo/mesh.ts` | the laptop node: opens and seals messages |
| `src/routes` | `/` sign in and sign up, `/chat` (chat list, `?with=<name>` opens one), `/devices`, `/settings` |
| `src/service-worker.ts` | offline copy of the app (PWA). ENS and relayer calls always go to the network |

If the browser's Web Serial drops the boards ("The device has been lost", seen with Chrome on Linux), use the local serial helper instead:

```sh
cd firmware/tools && python serial_helper.py   # venv from firmware/README.md
```

and set `PUBLIC_SERIAL_HELPER=ws://127.0.0.1:8765` in `.env`. The app then lists the ports itself.

ENS tests run against the local fork: start `contracts/script/local-fork.sh`, then export the `KINJO_*` lines it prints before `pnpm test`.
