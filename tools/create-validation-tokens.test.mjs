import assert from "node:assert/strict";
import test from "node:test";

import { generateValidationTokens } from "./create-validation-tokens.mjs";

const credentials = {
  apiKey: "validation-key",
  apiSecret: "validation-secret-at-least-32-characters",
  serverUrl: "ws://192.0.2.10:7880",
  room: "qg1-test",
  ttlMinutes: 60,
};

function decodeJwt(token) {
  const [header, payload, signature] = token.split(".");
  assert.ok(header);
  assert.ok(payload);
  assert.ok(signature);
  return {
    header: JSON.parse(Buffer.from(header, "base64url").toString("utf8")),
    payload: JSON.parse(Buffer.from(payload, "base64url").toString("utf8")),
  };
}

test("creates least-privilege host and viewer validation tokens", async () => {
  const result = await generateValidationTokens(credentials);
  const host = decodeJwt(result.host.token);
  const viewer = decodeJwt(result.viewer.token);

  assert.equal(result.serverUrl, credentials.serverUrl);
  assert.equal(result.viewer.publisherIdentity, "validation-host");
  assert.equal(host.header.alg, "HS256");
  assert.equal(host.payload.iss, credentials.apiKey);
  assert.equal(host.payload.sub, "validation-host");
  assert.deepEqual(host.payload.video, {
    roomJoin: true,
    room: "qg1-test",
    canPublish: true,
    canSubscribe: false,
    canPublishData: false,
    canUpdateOwnMetadata: false,
  });
  assert.equal(viewer.payload.sub, "validation-viewer");
  assert.equal(viewer.payload.video.canPublish, false);
  assert.equal(viewer.payload.video.canSubscribe, true);
  assert.equal(host.payload.exp - host.payload.nbf, 60 * 60);
});

test("rejects a token lifetime shorter than the validation run", async () => {
  await assert.rejects(
    generateValidationTokens({ ...credentials, ttlMinutes: 30 }),
    /35 to 120 minutes/,
  );
});

test("rejects identical host and viewer identities", async () => {
  await assert.rejects(
    generateValidationTokens({
      ...credentials,
      hostIdentity: "same-participant",
      viewerIdentity: "same-participant",
    }),
    /must be different/,
  );
});
