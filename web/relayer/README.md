# Kinjo relayer

Submits owner-signed `KinjoOnboarding` requests and pays the gas, so people can join without Sepolia ETH. Design: [docs/onboarding.md](../../docs/onboarding.md).

| Endpoint | Does |
|---|---|
| `POST /relay` | takes `{ action, owner, ..., deadline, signature }` (see `web/app/src/lib/kinjo/onboarding.ts`), simulates the call, sends it, returns `{ hash }` or `{ error }` |
| `GET /health` | relayer address, contract, chain |
| any other `GET` | the built web app, if `STATIC_DIR` is set (the Docker image does) |

Checks before any transaction: request shape, deadline at most 1 hour ahead, 10 requests per owner per 10 minutes. The contract then checks the signature and nonce.

## Run

Needs Node 22.18 or newer (it runs the TypeScript directly) and `pnpm install` in both `web/relayer` and `web/app` (the request format is shared).

| Variable | Value |
|---|---|
| `KINJO_ONBOARDING` | contract address (required) |
| `RELAYER_PRIVATE_KEY` | key of the `kinjo.eth` wallet, only in the server's env file |
| `KINJO_RPC_URL` | default: public Sepolia RPC |
| `ALLOWED_ORIGIN` | web app origins, comma-separated (default `*`) |
| `PORT` | default `8787` |
| `WORLD_APP_ID`, `WORLD_RP_ID`, `WORLD_RP_SIGNING_KEY` | World ID badge, from [developer.world.org](https://developer.world.org). Unset: `/world/*` answers 503 |
| `WORLD_ENVIRONMENT` | `staging` (default, works with the [simulator](https://simulator.worldcoin.org)) or `production` |
| `WORLD_STAGING_TOKEN` | staging only: token from opening a staging verification window in the Developer Portal (24 h) |
| `WORLD_NULLIFIER_FILE` | default `data/world-nullifiers.json` |

```sh
pnpm start
```

The staging simulator has a single identity, so after one wallet verifies, every other wallet is refused. To test again, reset the list (on-chain badges stay):

```sh
docker compose exec kinjo sh -c 'echo {} > /data/world-nullifiers.json'
```

`pnpm test` runs the World ID checks (`world.test.ts`) without network. The fork tests below need the fork.

## Test against the local fork

```sh
../../contracts/script/local-fork.sh       # other terminal, prints the variables below
export KINJO_RPC_URL=http://127.0.0.1:8545 KINJO_ONBOARDING=0x... KINJO_TEAM=0x2817...
pnpm test
```

On the fork no key is needed: `KINJO_TEAM` is sent from directly.
