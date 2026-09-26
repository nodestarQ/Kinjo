# Kinjo web: the static app and the relayer in one container. Build context: web/
# Public app settings are build arguments (they end up in the static files).

FROM node:22-alpine AS app
RUN corepack enable
WORKDIR /src/app
COPY app/package.json app/pnpm-lock.yaml ./
RUN pnpm install --frozen-lockfile
COPY app/ ./
ARG PUBLIC_KINJO_CHAIN_ID=11155111
ARG PUBLIC_KINJO_RPC_URL=https://ethereum-sepolia-rpc.publicnode.com
ARG PUBLIC_KINJO_ONBOARDING
ARG PUBLIC_RELAYER_URL
ARG PUBLIC_KINJO_TEAM=0x281770ab3731C474a7F7ab00FfE0A4A92Bf6aCaD
ARG PUBLIC_SERIAL_HELPER=
RUN printf '%s\n' \
      "PUBLIC_KINJO_CHAIN_ID=$PUBLIC_KINJO_CHAIN_ID" "PUBLIC_KINJO_RPC_URL=$PUBLIC_KINJO_RPC_URL" \
      "PUBLIC_KINJO_ONBOARDING=$PUBLIC_KINJO_ONBOARDING" "PUBLIC_RELAYER_URL=$PUBLIC_RELAYER_URL" \
      "PUBLIC_KINJO_TEAM=$PUBLIC_KINJO_TEAM" "PUBLIC_SERIAL_HELPER=$PUBLIC_SERIAL_HELPER" > .env \
 && pnpm build

FROM node:22-alpine
WORKDIR /srv/web/relayer
COPY relayer/package.json ./
RUN npm install --omit=dev --no-audit --no-fund
COPY relayer/server.ts ./
# The relayer imports the request format from the app source; viem resolves from the relayer.
COPY app/src/lib/kinjo/onboarding.ts /srv/web/app/src/lib/kinjo/onboarding.ts
RUN ln -s /srv/web/relayer/node_modules /srv/web/app/node_modules
COPY --from=app /src/app/build /srv/web/static
ENV STATIC_DIR=/srv/web/static PORT=8787
USER node
EXPOSE 8787
CMD ["node", "server.ts"]
