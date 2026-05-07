# studio/api/Dockerfile
#
# Multi-stage build aiming for < 250 MiB final image.
#
#   stage 1 (builder): node:20-alpine + dev deps + tsc compile
#   stage 2 (runtime): node:20-alpine + prod deps + dist/
#
# Cold start budget < 800 ms is met by:
#   - alpine base (no glibc startup tax)
#   - lazy-init OTel SDK in src/observability/tracer.ts
#   - drizzle-orm (no codegen runtime)

FROM node:20-alpine AS builder
WORKDIR /app

# Native build dep for some Node addons; alpine ships busybox but not gcc.
RUN apk add --no-cache --virtual .build-deps python3 make g++

COPY package.json package-lock.json* ./
RUN npm ci --no-audit --no-fund

COPY tsconfig.json ./
COPY src ./src
RUN npm run build

# Prune dev deps so we copy only prod deps to the runtime stage.
RUN npm prune --omit=dev

# ---------------------------------------------------------------------------

FROM node:20-alpine AS runtime
WORKDIR /app

# Run as non-root.
RUN addgroup -S scc && adduser -S scc -G scc

ENV NODE_ENV=production
ENV NODE_OPTIONS="--enable-source-maps"

COPY --from=builder --chown=scc:scc /app/node_modules ./node_modules
COPY --from=builder --chown=scc:scc /app/dist          ./dist
COPY --from=builder --chown=scc:scc /app/package.json  ./package.json

USER scc
EXPOSE 8080
HEALTHCHECK --interval=10s --timeout=2s --start-period=15s --retries=3 \
    CMD wget -q -O- http://127.0.0.1:8080/health || exit 1

CMD ["node", "dist/index.js"]
