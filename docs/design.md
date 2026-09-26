# Kinjo design

## Nodes

| Node | Job |
|---|---|
| Handheld | write, draw, send, sign payments |
| Relay | forward traffic it can't read |
| Laptop node | desktop UI and gateway to Ethereum |

Any cheap device with a radio could be a node later: toys, tools, home appliances. Parts we use: [hardware.md](hardware.md).

The demo forces a two-hop route (handheld → relay → laptop) so the relay really has to forward.

## How a message travels

1. The handheld encrypts the message for the recipient (X25519 + ChaCha20-Poly1305).
2. It sends it over ESP-NOW. No router or Internet needed.
3. Each node drops duplicates, lowers the hop count and forwards the rest.
4. Only the recipient can decrypt it. The relay just sees ciphertext.

## Where ENS comes in

- Every person has a name like `alice.kinjo.eth`.
- Every device gets a subname like `handheld.alice.kinjo.eth` with its public keys in text records.
- Devices sync these records while online and keep a local copy, so trust checks still work offline.
- The owner can revoke a device in ENS. Other nodes reject it after their next sync.

## Who you hear from

Each chat interface (handheld, laptop node) decides what its own screen shows. Nothing here drops or censors traffic: relays forward every packet because they can't read it.

- **Verified humans only:** an owner can verify with World ID. The badge then covers all their devices. With the filter on, messages from unverified senders are hidden but not deleted. The UI shows how many are hidden.
- **Block:** block a whole owner (`alice.kinjo.eth`, including devices she adds later) or a single device. Blocked messages are hidden the same way. The block list stays on the device.
- The handheld stores up to 256 blocks and 64 contacts in flash.

## Payments without Internet

- The handheld signs a payment (an EIP-712 "intent") and sends it over the mesh like any other message.
- Any node with Internet can submit it to `KinjoVault` on Sepolia. It can refuse, but it can't change the payment.
- The vault checks the signature, a spend limit and that the device is still authorized in ENS.
- A receipt comes back over the mesh.

Devices never hold funds. Money stays in the owner's vault, so a lost device costs at most its spend limit.

## Limits

- Testnet only. ESP32 keys sit in plain flash.
- No anonymity: relays can see that traffic exists, just not what it says.
- ESP-NOW reaches tens of meters. City range would need LoRa.
- The handheld trusts the gateway's receipt.

## Roadmap

- LoRa for longer range
- Proper mesh routing instead of flooding
- Several independent gateways
- Stealth payments with keys published in ENS
