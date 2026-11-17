import assert from "node:assert/strict";
import test from "node:test";

import { evaluateReports } from "./qg1-evaluate.mjs";

function endpoint(role) {
  const viewer = role === "viewer";
  return {
    schema: "screen-share.qg1.endpoint-report.v1",
    endpoint_role: role,
    started_at_utc: "2026-11-17T00:00:00.000Z",
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

test("rejects reports with swapped endpoint roles", () => {
  assert.throws(
    () => evaluateReports(endpoint("viewer"), endpoint("host"), passingManualEvidence),
    /expected a host report/,
  );
});
