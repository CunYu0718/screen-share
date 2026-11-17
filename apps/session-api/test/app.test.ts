import assert from "node:assert/strict";
import test from "node:test";

import { buildApp } from "../src/app.js";

test("health endpoint reports the service version", async (context) => {
  const app = buildApp();
  context.after(async () => app.close());

  const response = await app.inject({ method: "GET", url: "/healthz" });

  assert.equal(response.statusCode, 200);
  assert.deepEqual(response.json(), {
    service: "session-api",
    status: "ok",
    version: "0.1.0",
  });
});
test("capabilities match the product contract", async (context) => {
  const app = buildApp();
  context.after(async () => app.close());

  const response = await app.inject({ method: "GET", url: "/v1/capabilities" });
  const body = response.json();

  assert.equal(response.statusCode, 200);
  assert.equal(body.maximumViewers, 20);
  assert.equal(body.maximumParticipants, 21);
  assert.deepEqual(
    body.qualityProfiles.map((profile: { name: string }) => profile.name),
    ["720p30", "1080p60", "1440p60"],
  );
});

test("unknown routes use the public error envelope", async (context) => {
  const app = buildApp();
  context.after(async () => app.close());

  const response = await app.inject({
    headers: { "x-request-id": "test-request-id" },
    method: "GET",
    url: "/missing",
  });

  assert.equal(response.statusCode, 404);
  assert.deepEqual(response.json(), {
    error: {
      code: "route_not_found",
      message: "Route not found",
      requestId: "test-request-id",
    },
  });
});
