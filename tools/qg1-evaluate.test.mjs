import assert from "node:assert/strict";
import test from "node:test";

import { evaluateReports } from "./qg1-evaluate.mjs";

function endpoint(role) {
  const viewer = role === "viewer";
  return {
    schema: "screen-share.qg1.endpoint-report.v2",
    endpoint_role: role,
    app_version: "0.1.0",
    build_commit: "0123456789abcdef0123456789abcdef01234567",
    livekit_sdk_version: "1.13.1",
    started_at_utc: "2026-11-17T00:00:00.000Z",
    system: {
      os: "Windows 11",
      gpu: {
        description: viewer ? "Viewer GPU" : "Host GPU",
        driver_version: "32.0.1.2",
      },
    },
    environment: {
      sfu_version: "1.13.11",
      network_baseline: {
        upstream_mbps: 100,
        downstream_mbps: 100,
        rtt_ms: 12,
        packet_loss_ratio: 0.001,
      },
    },
    summary: {
      average_packet_loss_ratio: 0.002,
      automated_checks: {
        duration_30_minutes: true,
        endpoint_pipeline_metrics_complete: true,
        memory_growth_at_most_10_percent: true,
        media_active_for_all_samples: true,
        average_render_at_least_55_fps: viewer ? true : null,
        no_observed_render_gap_over_500_ms: viewer ? true : null,
      },
    },
    samples: [
      {
        media_active: true,
        width: 2560,
        height: 1440,
      },
    ],
  };
}

const passingManualEvidence = {
  latencyP95Ms: 280,
  noVisualCorruption: true,
  noCrash: true,
  noDeviceLoss: true,
};

test("passes only when both endpoints and manual evidence satisfy QG-1", () => {
  const result = evaluateReports(endpoint("host"), endpoint("viewer"), passingManualEvidence);
  assert.equal(result.qg1_status, "pass");
  assert.equal(Object.values(result.checks).every(Boolean), true);
});

test("fails when viewer rendering is below the acceptance threshold", () => {
  const viewer = endpoint("viewer");
  viewer.summary.automated_checks.average_render_at_least_55_fps = false;
  const result = evaluateReports(endpoint("host"), viewer, passingManualEvidence);
  assert.equal(result.qg1_status, "fail");
  assert.equal(result.checks.viewer_average_render_at_least_55_fps, false);
});

test("fails when manual evidence is omitted", () => {
  const result = evaluateReports(endpoint("host"), endpoint("viewer"), {
    latencyP95Ms: 280,
  });
  assert.equal(result.qg1_status, "fail");
  assert.equal(result.checks.no_process_crash, false);
});

test("fails when endpoints use different client commits", () => {
  const viewer = endpoint("viewer");
  viewer.build_commit = "abcdef0123456789abcdef0123456789abcdef01";
  const result = evaluateReports(endpoint("host"), viewer, passingManualEvidence);
  assert.equal(result.qg1_status, "fail");
  assert.equal(result.checks.matching_client_build, false);
});

test("fails when the host network misses the QG-1 baseline", () => {
  const host = endpoint("host");
  host.environment.network_baseline.upstream_mbps = 20;
  const result = evaluateReports(host, endpoint("viewer"), passingManualEvidence);
  assert.equal(result.qg1_status, "fail");
  assert.equal(result.checks.host_upstream_at_least_30_mbps, false);
});

test("rejects reports with swapped endpoint roles", () => {
  assert.throws(
    () => evaluateReports(endpoint("viewer"), endpoint("host"), passingManualEvidence),
    /expected a host report/,
  );
});
