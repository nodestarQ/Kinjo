# Kinjo

> 近所 (きんじょ, *kinjo*): "neighborhood"

A project for **ETHGlobal Tokyo 2026**.

**Live:** [kinjo.nodestarq.com](https://kinjo.nodestarq.com) (Sepolia, ENSv2)

## Why we want to do this

We wanted to do something a bit different this time and build with hardware.

Much of daily life depends on infrastructure that we only notice when it fails, for example: the 2025 blackout in Spain and Portugal, earthquakes, tsunamis, typhoons or networks that are shut down on purpose. When that happens, people nearby often can't reach each other, let alone send or receive money, even when they are only a few meters apart.

## Our Idea

Kinjo is an experiment in keeping a neighborhood connected when the usual infrastructure isn't there:

- small, cheap devices that talk directly to each other
- devices in the neighbourhood help carry each other's messages
- the trust layer is built on top of Ethereum and ENS

## What works

- **Handheld** (ESP32 with a 2.8" touch screen and 3 buttons): write with an on-screen keyboard, draw in 8 colors, send both as one note. Runs from a power bank.
- **Relay** (XIAO ESP32-C3): forwards everything, can read nothing. Its log only shows headers and ciphertext.
- **Web app**, also the laptop node: sign up for a name, register devices, chat over the radio bridge. Installable and usable offline (PWA).
- **End-to-end encryption** (X25519 + ChaCha20-Poly1305) over a forced 2-hop route: handheld → relay → laptop.
- **ENSv2 on Sepolia** as the trust layer: names, device keys, revocation. A World ID badge is in the contract but not yet switched on in the app.

How it works: [docs/design.md](docs/design.md). Wire format: [protocol/SPEC.md](protocol/SPEC.md).

## How we use ENSv2

| | |
|---|---|
| `kinjo.eth` | its own subname registry (ENSv2 `UserRegistry`), run by our `KinjoOnboarding` contract |
| `alice.kinjo.eth` | claimed for free. Alice gets her **own registry** and her **own PermissionedResolver**. She holds every role on both |
| `handheld.alice.kinjo.eth` | one subname per device. Its X25519 key is the text record `xyz.kinjo.encryption-key` |
| revocation | Alice removes the subname and clears the key in one transaction. Peers reject the device at their next sync |
| `alice.verified.kinjo.eth` | World ID badge, owned by the team, `addr` bound to Alice so it can't be faked or carried over |
| gas | users only sign (EIP-712). A relayer submits, so nobody needs Sepolia ETH |

Details: [docs/onboarding.md](docs/onboarding.md). Contract: [contracts/](contracts/README.md), tested on a Sepolia fork against the real ENSv2 contracts.

## Try the demo

1. Open the live site in Chrome or Edge, **Register** with a wallet and pick a name. This browser becomes `laptop.<name>.kinjo.eth`.
2. **Devices**: plug in the handheld over USB, connect it, give it a name, **Register device**. It gets its name and contacts right away.
3. **Chat**: plug in the radio bridge, **Connect bridge**. Send a note from the handheld and it arrives here as **verified**. Reply from the web app and it shows on the handheld.
4. **Devices → Revoke** the handheld. Its next message still arrives (the relay can't tell) but shows **REVOKED**.

Hardware and wiring: [docs/hardware.md](docs/hardware.md). Building and flashing: [firmware/](firmware/README.md). Linux browsers may drop USB boards: see [web/app](web/app/README.md) for the local serial helper.

## Run it locally

Everything runs against a local Sepolia fork, so no testnet ETH is needed:

1. `contracts/script/local-fork.sh` starts the fork and prints the `KINJO_*` settings ([contracts](contracts/README.md#local-fork)).
2. Start the relayer with those settings ([web/relayer](web/relayer/README.md)).
3. `cd web/app && pnpm install && pnpm dev` with `.env` pointing at the fork ([web/app](web/app/README.md)).

Deploying the app and relayer with Docker: [web/deploy](web/deploy/README.md).

## Known limits

- Renaming a device takes two signatures (add the new name, revoke the old one).
- Chrome on Linux loses USB boards, hence the serial helper. Chrome and Edge on macOS work directly.
- More in [docs/design.md](docs/design.md#limits).

## Structure

```text
protocol/    wire format spec, Python reference, shared test vectors
firmware/    ESP32 code: handheld, relay, radio bridge, shared C++ library, tools
web/app/     web app and laptop node (SvelteKit, installable)
laptop/      placeholder, the laptop node lives in web/app
web/relayer/ relayer for gas-free onboarding, serves the app in Docker
contracts/   KinjoOnboarding (Foundry)
docs/        design, onboarding, hardware
```

## Tests

| What | Run | Details |
|---|---|---|
| Protocol reference, test vectors | `cd protocol/reference && .venv/bin/python -m unittest` | [protocol/reference](protocol/reference/README.md) |
| Firmware library (runs on the laptop) | `make -C firmware/test` | needs a C++ compiler and python3 |
| Contracts (Sepolia fork) | `cd contracts && forge test` | [contracts](contracts/README.md) |
| Web app protocol port | `cd web/app && pnpm test` | [web/app](web/app/README.md) |
| Relayer (local fork) | `cd web/relayer && pnpm test` | [web/relayer](web/relayer/README.md) |

## Team

_Coming soon._

## Roadmap

See [docs/design.md](docs/design.md#roadmap).
