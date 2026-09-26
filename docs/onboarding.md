# Onboarding

How a person joins Kinjo and gets a hardware device into the network. Wire format: [protocol/SPEC.md](../protocol/SPEC.md).

## In one sentence

The owner plugs the handheld into a laptop, opens the web app and signs one message. The device then becomes `handheld.alice.kinjo.eth` with its public key in ENS.

## Names

```text
kinjo.eth                    team, registry owned by the team wallet
  alice.kinjo.eth            owner, claimed for free, own registry and resolver
    handheld.alice.kinjo.eth device, key in a text record
    laptop.alice.kinjo.eth   device
```

- ENSv2 on Sepolia, contracts tagged `sepolia-deployment-2026-09-15`.
- Every owner gets **their own subname registry** (for devices) and **their own PermissionedResolver** (for records). Resolver permissions are per record key, not per name, so a shared resolver would let owners overwrite each other.
- The owner holds all roles on their registry and resolver. Kinjo can't lock them out.

Records:

| Name | Key | Value |
|---|---|---|
| device | `xyz.kinjo.encryption-key` | X25519 public key, `0x` hex |
| device | `xyz.kinjo.kind` | `device` |
| device | `xyz.kinjo.protocol` | `kinjo/0.1` |
| owner | `xyz.kinjo.verified-human` | `world` (optional, see World ID below) |

## Contract: `KinjoOnboarding`

One contract, deployed once by the team. It holds the registrar and unregister roles on the `kinjo.eth` registry. Each call does all ENSv2 steps in **one transaction**.

| Function | Does |
|---|---|
| `join(label, deviceLabel, deviceKey)` | deploys the owner's registry and resolver, registers `label.kinjo.eth`, links the registry, registers the first device, writes its records |
| `addDevice(deviceLabel, deviceKey)` | registers another device under the owner and writes its records |
| `revokeDevice(deviceLabel)` | unregisters the device and clears `xyz.kinjo.encryption-key` (ENSv2 falls back to the parent's resolver, so clearing the record is needed too) |
| `setVerified(owner)` | team only, writes `xyz.kinjo.verified-human` |
| `release(label)` | team only, unregisters `label.kinjo.eth` so the name can be claimed again |

Each user function has two ways in:

- **Sponsored (now):** the owner signs an EIP-712 request (owner, action, arguments, nonce, deadline). Anyone can submit it. The contract checks the signature. Our relayer submits and pays the gas.
- **Self-paid (later):** the owner calls the same function directly. `msg.sender` is the owner, no signature needed. A fee (owner-settable, `0` for now) can make it payable.

On `join` the owner's registry and resolver grant the contract only the roles it needs (register, unregister, set text), so the sponsored path also works for later calls.

## Relayer

Small service on the Hetzner server next to the web app (`web/relayer`, behind Caddy).

- `POST /relay` with `{ action, request, signature }`. Checks the signature and deadline, simulates, sends the transaction from the team wallet, returns the tx hash.
- Rate limit per owner address. Sepolia only.
- The team wallet key lives only in the server's environment file.

## Web app flow

Runs in Chrome or Edge (Web Serial needs them and HTTPS).

1. **Connect wallet.** Only for signing. No Sepolia ETH needed.
2. **Pick a name**, e.g. `alice`. The app checks it's free.
3. **Plug in the handheld** and click "Connect device". The app reads the device's public key over USB.
4. **Name the device**, e.g. `handheld`. Sign one message. The relayer submits `join`. Wait for the tx (~15 s), show it on Etherscan.
5. **Provision the device** over USB: its own ENS name plus the contacts it may talk to (name and key, read from ENS).
6. **Done.** The device shows `handheld.alice.kinjo.eth`. The laptop node verifies its messages against ENS.

Already joined: step 4 becomes `addDevice`.

## Device side (USB)

The handheld makes its X25519 key pair on first boot and keeps it in flash. The private key never leaves the device.

Commands use the serial framing of SPEC §12, frame type `PROVISION`. Details go in SPEC §13:

| Command | Device answer |
|---|---|
| `INFO` | public key, own name (empty if new), number of contacts, firmware version |
| `SET_NAME` name | `OK` |
| `ADD_CONTACT` name + key + verified flag | `OK` |
| `CLEAR_CONTACTS` | `OK` |
| `WIPE` | new key pair, name and contacts cleared, then `INFO` |

## Demo reset

To show onboarding again from scratch:

| Button | Does |
|---|---|
| **Reset demo** | `revokeDevice` (sponsored), then `WIPE` over USB, then forget the wallet session in the browser |
| **Release name** (team wallet only) | `release(label)`, so the same name can be claimed again |

After a reset the device has a new key, so the old ENS record could never match it again even if the revoke failed.

## Revocation

The owner clicks "Revoke" on a device (sponsored `revokeDevice`). At its next ENS sync the laptop node marks the device revoked and rejects its messages. Relays keep forwarding them because they can't read them (SPEC §11).

## World ID (optional badge)

Claims stay open without it. After joining, the owner can click "Verify with World ID". The relayer checks the proof and calls `setVerified(owner)`, which writes `xyz.kinjo.verified-human` on `alice.kinjo.eth`. The badge is pushed to devices with the contact list and powers the "verified humans only" filter ([design.md](design.md#who-you-hear-from)). Nothing is blocked without it.

## Costs

The team pays gas for every sponsored call on Sepolia. `kinjo.eth` itself is registered until 2027-09-25.
