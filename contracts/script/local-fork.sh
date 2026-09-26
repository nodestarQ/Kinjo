#!/usr/bin/env bash
# Local Sepolia fork on http://127.0.0.1:8545 (chain ID 31337) with Kinjo: the Sepolia deployment
# if there is one, else a fresh deploy as the kinjo.eth wallet.
# Any address can send transactions without its key. Ctrl+C stops it.
set -euo pipefail
cd "$(dirname "$0")/.."

# Needs an RPC that keeps old state: the fork reads Sepolia as of its start block for hours.
RPC=${SEPOLIA_RPC_URL:-https://sepolia.gateway.tenderly.co}
TEAM=0x281770ab3731C474a7F7ab00FfE0A4A92Bf6aCaD
LOCAL=http://127.0.0.1:8545

anvil --fork-url "$RPC" --chain-id 31337 --auto-impersonate --silent &
ANVIL=$!
trap 'kill $ANVIL' EXIT
until cast chain-id --rpc-url $LOCAL >/dev/null 2>&1; do sleep 0.5; done

# Kinjo already on Sepolia: the fork has a copy of it (names included). Otherwise deploy it here.
SEPOLIA_RUN=broadcast/Deploy.s.sol/11155111/run-latest.json
if [ -f "$SEPOLIA_RUN" ]; then
  ONBOARDING=$(jq -r '.transactions[] | select(.contractName == "KinjoOnboarding") | .contractAddress' "$SEPOLIA_RUN")
else
  forge script script/Deploy.s.sol --rpc-url $LOCAL --sender $TEAM --unlocked --broadcast --slow >/dev/null
  ONBOARDING=$(jq -r '.transactions[] | select(.contractName == "KinjoOnboarding") | .contractAddress' \
    broadcast/Deploy.s.sol/31337/run-latest.json)
fi

echo "Local fork ready on $LOCAL"
echo "KINJO_RPC_URL=$LOCAL"
echo "KINJO_ONBOARDING=$ONBOARDING"
echo "KINJO_TEAM=$TEAM"
wait $ANVIL
