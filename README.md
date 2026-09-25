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

## How it works (rough plan)

### Hardware (e.g.: small toys, tools or home appliances)

- **Handheld:** an ESP32 with a small touchscreen and a few buttons to write, draw and send
- **Relay:** a tiny ESP32-C3 board without a screen that just forwards traffic for its neighbors
- **Laptop node:** a laptop with an ESP32-C3 plugged in over USB as its radio

The devices talk directly to each other over ESP-NOW (Wi-Fi radio which means that no router or access point is needed)(LoRa could extend the range later). Messages are end-to-end encrypted, so relays pass them on without being able to read them.

```
handheld  ──►  relay  ──►  laptop node
 (ESP32)     (ESP32-C3)   (+ ESP32-C3)
```

### Ethereum / ENS

- Each person has an ENS name (e.g. `alice.kinjo.eth`), and each of their devices gets a subname (e.g. `handheld.alice.kinjo.eth`) that publishes its keys
- Devices check each other against ENS, so the network knows which device belongs to whom, and an owner can revoke a lost device
- devices that do have Internet act as gateways and pass signed transactions from offline devices on to Ethereum

## Structure

_Coming soon._

## Getting started

_Coming soon._

## Team

_Coming soon._
