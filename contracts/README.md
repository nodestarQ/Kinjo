# Contracts

`KinjoOnboarding` puts people and devices into ENSv2 under `kinjo.eth` on Sepolia. Design: [docs/onboarding.md](../docs/onboarding.md).

| File | What |
|---|---|
| `src/KinjoOnboarding.sol` | join, add and revoke devices (owner or sponsored with an EIP-712 signature), team release and World ID badge names |
| `src/IENSv2.sol` | the ENSv2 functions and roles we use (contracts-v2 tag `sepolia-deployment-2026-09-15`) |
| `test/KinjoOnboarding.t.sol` | tests on a Sepolia fork against the real ENSv2 contracts |
| `script/Deploy.s.sol` | one-time setup by the `kinjo.eth` owner wallet, shared with the tests |

Needs [Foundry](https://getfoundry.sh).

```sh
cd contracts
forge install --no-git foundry-rs/forge-std@v1.10.0 OpenZeppelin/openzeppelin-contracts@v5.4.0
forge test
```

Tests fork the latest Sepolia block through a public RPC. Set `SEPOLIA_RPC_URL` to use your own. `FORK_BLOCK` pins a block but needs an archive RPC.

## Local fork

For building the relayer and web app without touching the real Sepolia:

```sh
./script/local-fork.sh
```

It starts `anvil` as a Sepolia fork on `http://127.0.0.1:8545` (chain ID 31337), deploys Kinjo as the `kinjo.eth` wallet and prints `KINJO_ONBOARDING`. Any address can send transactions there without its key.

## Deploy

Run once with the wallet that owns `kinjo.eth`:

```sh
forge script script/Deploy.s.sol --rpc-url $SEPOLIA_RPC_URL --account <keystore name> --broadcast
```

It sets up `kinjo.eth` and `verified.kinjo.eth` (about 5.5M gas, 0.012 Sepolia ETH) and prints the addresses. The web app and relayer need the `KinjoOnboarding` one. A second deploy by the same wallet needs a new salt: `KINJO_SALT=10 forge script ...`.

Owner addresses must accept ERC-1155 tokens, because ENSv2 mints each name as one. Plain accounts do. A smart account without an ERC-1155 receiver can't join.
