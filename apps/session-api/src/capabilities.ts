import type { CapabilitiesResponse } from "./contracts.js";

export const capabilities = {
  maximumViewers: 20,
  maximumParticipants: 21,
  qualityProfiles: [
    {
      name: "720p30",
      width: 1280,
      height: 720,
      framesPerSecond: 30,
      minimumBitrateBps: 2_000_000,
      maximumBitrateBps: 4_000_000,
    },
    {
      name: "1080p60",
      width: 1920,
      height: 1080,
      framesPerSecond: 60,
      minimumBitrateBps: 6_000_000,
      maximumBitrateBps: 10_000_000,
    },
    {
      name: "1440p60",
      width: 2560,
      height: 1440,
      framesPerSecond: 60,
      minimumBitrateBps: 12_000_000,
      maximumBitrateBps: 20_000_000,
    },
  ],
} as const satisfies CapabilitiesResponse;
