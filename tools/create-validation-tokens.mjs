import { readFile } from "node:fs/promises";
import { pathToFileURL } from "node:url";

import { AccessToken } from "livekit-server-sdk";

const identityPattern = /^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$/;

function requireIdentity(value, label) {
  if (!identityPattern.test(value)) {
    throw new Error(`${label} must use 1-128 ASCII letters, numbers, dots, underscores, or hyphens`);
  }
  return value;
}

function requireServerUrl(value) {
  let parsed;
  try {
    parsed = new URL(value);
  } catch {
    throw new Error("server URL must be a valid ws:// or wss:// URL");
  }
  if (!['ws:', 'wss:'].includes(parsed.protocol)) {
    throw new Error("server URL must use ws:// or wss://");
  }
  return parsed.toString().replace(/\/$/, "");
}

export async function generateValidationTokens({
  apiKey,
  apiSecret,
  serverUrl,
  room = "qg1-validation",
  hostIdentity = "validation-host",
  viewerIdentity = "validation-viewer",
  ttlMinutes = 60,
}) {
  if (typeof apiKey !== "string" || apiKey.length === 0) {
    throw new Error("LIVEKIT_API_KEY is required");
  }
  if (typeof apiSecret !== "string" || apiSecret.length < 32) {
    throw new Error("LIVEKIT_API_SECRET must contain at least 32 characters");
  }
  const normalizedServerUrl = requireServerUrl(serverUrl);
  const normalizedRoom = requireIdentity(room, "room");
  const normalizedHostIdentity = requireIdentity(hostIdentity, "host identity");
  const normalizedViewerIdentity = requireIdentity(viewerIdentity, "viewer identity");
  const normalizedTtlMinutes = Number(ttlMinutes);
  if (!Number.isInteger(normalizedTtlMinutes) || normalizedTtlMinutes < 35 ||
      normalizedTtlMinutes > 120) {
    throw new Error("TTL must be an integer from 35 to 120 minutes");
  }
  if (normalizedHostIdentity === normalizedViewerIdentity) {
    throw new Error("host and viewer identities must be different");
  }

  const ttlSeconds = normalizedTtlMinutes * 60;
  const host = new AccessToken(apiKey, apiSecret, {
    identity: normalizedHostIdentity,
    ttl: ttlSeconds,
  });
  host.addGrant({
    roomJoin: true,
    room: normalizedRoom,
    canPublish: true,
    canSubscribe: false,
    canPublishData: false,
    canUpdateOwnMetadata: false,
  });

  const viewer = new AccessToken(apiKey, apiSecret, {
    identity: normalizedViewerIdentity,
    ttl: ttlSeconds,
  });
  viewer.addGrant({
    roomJoin: true,
    room: normalizedRoom,
    canPublish: false,
    canSubscribe: true,
    canPublishData: false,
    canUpdateOwnMetadata: false,
  });

  return {
    serverUrl: normalizedServerUrl,
    room: normalizedRoom,
    expiresInSeconds: ttlSeconds,
    host: {
      identity: normalizedHostIdentity,
      token: await host.toJwt(),
      permissions: { canPublish: true, canSubscribe: false },
    },
    viewer: {
      identity: normalizedViewerIdentity,
      publisherIdentity: normalizedHostIdentity,
      token: await viewer.toJwt(),
      permissions: { canPublish: false, canSubscribe: true },
    },
  };
}

function parseArguments(argumentsList) {
  const allowedNames = new Set([
    "--env-file",
    "--server-url",
    "--room",
    "--host-identity",
    "--viewer-identity",
    "--ttl-minutes",
  ]);
  const values = new Map();
  for (let index = 0; index < argumentsList.length; index += 2) {
    const name = argumentsList[index];
    const value = argumentsList[index + 1];
    if (!name?.startsWith("--") || value === undefined) {
      throw new Error(`invalid argument: ${name ?? ""}`);
    }
    if (!allowedNames.has(name)) {
      throw new Error(`unknown argument: ${name}`);
    }
    values.set(name, value);
  }
  return values;
}

async function readEnvironmentFile(path) {
  if (!path) {
    return {};
  }
  const result = {};
  const contents = await readFile(path, "utf8");
  for (const rawLine of contents.split(/\r?\n/)) {
    const line = rawLine.trim();
    if (line.length === 0 || line.startsWith("#")) {
      continue;
    }
    const separator = line.indexOf("=");
    if (separator < 1) {
      throw new Error(`invalid environment line in ${path}`);
    }
    const name = line.slice(0, separator).trim();
    let value = line.slice(separator + 1).trim();
    if ((value.startsWith('"') && value.endsWith('"')) ||
        (value.startsWith("'") && value.endsWith("'"))) {
      value = value.slice(1, -1);
    }
    result[name] = value;
  }
  return result;
}

async function runCli() {
  const values = parseArguments(process.argv.slice(2));
  const fileEnvironment = await readEnvironmentFile(values.get("--env-file"));
  const environment = { ...fileEnvironment, ...process.env };
  const result = await generateValidationTokens({
    apiKey: environment.LIVEKIT_API_KEY,
    apiSecret: environment.LIVEKIT_API_SECRET,
    serverUrl: values.get("--server-url") ?? environment.LIVEKIT_URL ?? "ws://127.0.0.1:7880",
    room: values.get("--room") ?? "qg1-validation",
    hostIdentity: values.get("--host-identity") ?? "validation-host",
    viewerIdentity: values.get("--viewer-identity") ?? "validation-viewer",
    ttlMinutes: values.get("--ttl-minutes") ?? 60,
  });
  process.stderr.write(
    "Generated short-lived validation credentials. Do not save, commit, or upload this output.\n",
  );
  process.stdout.write(`${JSON.stringify(result, null, 2)}\n`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  runCli().catch((error) => {
    process.stderr.write(`${error.message}\n`);
    process.exitCode = 1;
  });
}
