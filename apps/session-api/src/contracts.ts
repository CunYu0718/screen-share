export const errorCodes = {
  internalError: "internal_error",
  invalidRequest: "invalid_request",
  routeNotFound: "route_not_found",
  roomFull: "room_full",
  roomNotFound: "room_not_found",
  tokenExpired: "token_expired",
  unauthorized: "unauthorized",
} as const;

export type ErrorCode = (typeof errorCodes)[keyof typeof errorCodes];

export interface ErrorResponse {
  error: {
    code: ErrorCode;
    message: string;
    requestId: string;
  };
}

export interface QualityProfile {
  name: "720p30" | "1080p60" | "1440p60";
  width: number;
  height: number;
  framesPerSecond: number;
  minimumBitrateBps: number;
  maximumBitrateBps: number;
}

export interface CapabilitiesResponse {
  maximumViewers: number;
  maximumParticipants: number;
  qualityProfiles: readonly QualityProfile[];
}
