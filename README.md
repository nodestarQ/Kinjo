# Kinjo

> 近所 (*kinjo*): "neighborhood"

A project for **ETHGlobal Tokyo 2026**. WIP

## Why we want to do this

We wanted to do something a bit different this time and build with hardware.

Much of daily life depends on infrastructure that we only notice when it fails, for example: the 2025 blackout in Spain and Portugal, earthquakes, tsunamis, typhoons or networks that are shut down on purpose. When that happens, people nearby often can't reach each other, let alone send or receive money, even when they are only a few meters apart.

## Our Idea

Kinjo is an experiment in keeping a neighborhood connected when the usual infrastructure isn't there:

- small, cheap devices that talk directly to each other
- devices in the neighbourhood help carry each other's messages
- the trust layer is built on top of Ethereum and ENS

How it works: [docs/design.md](docs/design.md). Hardware and wiring: [docs/hardware.md](docs/hardware.md).

## Structure

```text
protocol/    wire format spec, reference implementation, shared test vectors
firmware/    ESP32 code: handheld, relay, laptop radio bridge (shared lib)
laptop/      laptop node: serial bridge, desktop UI, gateway
web/         ENS onboarding web app
contracts/   smart contracts
docs/        design notes and hardware
```

## Getting started

_Coming soon._

## Tests

| What | Run | Details |
|---|---|---|
| Protocol reference, test vectors | `cd protocol/reference && .venv/bin/python -m unittest` | [protocol/reference](protocol/reference/README.md) |

## Team

_Coming soon._
