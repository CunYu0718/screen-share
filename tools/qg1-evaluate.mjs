import { readFile, writeFile } from "node:fs/promises";
import { pathToFileURL } from "node:url";

const endpointSchema = "screen-share.qg1.endpoint-report.v2";
const combinedSchema = "screen-share.qg1.combined-report.v1";

function requireEndpoint(report, role) {
  if (report?.schema !== endpointSchema) {
    throw new Error(`${role} report has an unsupported schema`);
  }
  if (report.endpoint_role !== role) {
    throw new Error(`expected a ${role} report, received ${report.endpoint_role ?? "unknown"}`);
  }
  if (!report.summary || !Array.isArray(report.samples)) {
    throw new Error(`${role} report is missing summary or samples`);
  }
}

function booleanCheck(report, name) {
  return report.summary?.automated_checks?.[name] === true;
}

function mediaResolutionAtLeast(report, minimumWidth, minimumHeight) {
  const activeSamples = report.samples.filter((sample) => sample.media_active === true);
  return (
    activeSamples.length > 0 &&
    activeSamples.every(
      (sample) => sample.width >= minimumWidth && sample.height >= minimumHeight,
    )
  );
}

function hardwareMetadataComplete(report) {
  const gpu = report.system?.gpu;
  return (
    typeof report.system?.os === "string" &&
    report.system.os.length > 0 &&
    typeof gpu?.description === "string" &&
    gpu.description.length > 0 &&
    typeof gpu?.driver_version === "string" &&
    gpu.driver_version.length > 0
  );
}

function networkBaseline(report) {
  return report.environment?.network_baseline ?? {};
}

function numberAtLeast(value, minimum) {
  return Number.isFinite(value) && value >= minimum;
}

function numberBetween(value, minimum, maximum) {
  return Number.isFinite(value) && value >= minimum && value <= maximum;
}

export function evaluateReports(host, viewer, manualEvidence) {
  requireEndpoint(host, "host");
  requireEndpoint(viewer, "viewer");

  const latencyP95Ms = Number(manualEvidence.latencyP95Ms);
  if (!Number.isFinite(latencyP95Ms) || latencyP95Ms < 0) {
    throw new Error("latencyP95Ms must be a non-negative number");
  }

  const checks = {
    matching_client_build:
      typeof host.build_commit === "string" &&
      host.build_commit !== "unknown" &&
      host.build_commit === viewer.build_commit,
    matching_client_version:
      typeof host.app_version === "string" &&
      host.app_version.length > 0 &&
      host.app_version === viewer.app_version,
    matching_livekit_sdk_version:
      typeof host.livekit_sdk_version === "string" &&
      host.livekit_sdk_version !== "unknown" &&
      host.livekit_sdk_version === viewer.livekit_sdk_version,
    matching_sfu_version:
      typeof host.environment?.sfu_version === "string" &&
      host.environment.sfu_version.length > 0 &&
      host.environment.sfu_version === viewer.environment?.sfu_version,
    host_hardware_metadata_complete: hardwareMetadataComplete(host),
    viewer_hardware_metadata_complete: hardwareMetadataComplete(viewer),
    host_upstream_at_least_30_mbps: numberAtLeast(
      networkBaseline(host).upstream_mbps,
      30,
    ),
    viewer_downstream_at_least_25_mbps:
      numberAtLeast(networkBaseline(viewer).downstream_mbps, 25),
    host_rtt_at_most_80_ms: numberBetween(networkBaseline(host).rtt_ms, 0, 80),
    viewer_rtt_at_most_80_ms: numberBetween(networkBaseline(viewer).rtt_ms, 0, 80),
    host_baseline_packet_loss_at_most_1_percent:
      numberBetween(networkBaseline(host).packet_loss_ratio, 0, 0.01),
    viewer_baseline_packet_loss_at_most_1_percent:
      numberBetween(networkBaseline(viewer).packet_loss_ratio, 0, 0.01),
    host_duration_30_minutes: booleanCheck(host, "duration_30_minutes"),
    viewer_duration_30_minutes: booleanCheck(viewer, "duration_30_minutes"),
    host_pipeline_metrics_complete: booleanCheck(
      host,
      "endpoint_pipeline_metrics_complete",
    ),
    viewer_pipeline_metrics_complete: booleanCheck(
      viewer,
      "endpoint_pipeline_metrics_complete",
    ),
    host_memory_growth_at_most_10_percent: booleanCheck(
      host,
      "memory_growth_at_most_10_percent",
    ),
    viewer_memory_growth_at_most_10_percent: booleanCheck(
      viewer,
      "memory_growth_at_most_10_percent",
    ),
    host_media_active_for_all_samples: booleanCheck(
      host,
      "media_active_for_all_samples",
    ),
    viewer_media_active_for_all_samples: booleanCheck(
      viewer,
      "media_active_for_all_samples",
    ),
    viewer_average_render_at_least_55_fps: booleanCheck(
      viewer,
      "average_render_at_least_55_fps",
    ),
    no_observed_render_gap_over_500_ms: booleanCheck(
      viewer,
      "no_observed_render_gap_over_500_ms",
    ),
    visual_latency_samples_sufficient: booleanCheck(
      viewer,
      "visual_latency_samples_sufficient",
    ),
    visual_latency_p95_at_most_500_ms: booleanCheck(
      viewer,
      "visual_latency_p95_at_most_500_ms",
    ),
    host_1440p_media: mediaResolutionAtLeast(host, 2560, 1440),
    viewer_1440p_media: mediaResolutionAtLeast(viewer, 2560, 1440),
    host_average_packet_loss_at_most_1_percent:
      host.summary.average_packet_loss_ratio <= 0.01,
    viewer_average_packet_loss_at_most_1_percent:
      viewer.summary.average_packet_loss_ratio <= 0.01,
    latency_p95_at_most_500_ms: latencyP95Ms <= 500,
    no_permanent_black_or_corrupt_frames: manualEvidence.noVisualCorruption === true,
    no_process_crash: manualEvidence.noCrash === true,
    no_unrecovered_device_loss: manualEvidence.noDeviceLoss === true,
  };

  return {
    schema: combinedSchema,
    evaluated_at_utc: new Date().toISOString(),
    qg1_status: Object.values(checks).every(Boolean) ? "pass" : "fail",
    client_build: {
      version: host.app_version,
      commit: host.build_commit,
      livekit_sdk_version: host.livekit_sdk_version,
    },
    sfu_version: host.environment?.sfu_version,
    endpoint_hardware: {
      host: host.system?.gpu,
      viewer: viewer.system?.gpu,
    },
    measured_latency_p95_ms: latencyP95Ms,
    visual_marker_latency_p95_ms: viewer.summary.visual_latency_p95_ms,
    endpoint_started_at_utc: {
      host: host.started_at_utc,
      viewer: viewer.started_at_utc,
    },
    checks,
  };
}

function parseArguments(argumentsList) {
  const values = new Map();
  const flags = new Set();
  for (let index = 0; index < argumentsList.length; index += 1) {
    const argument = argumentsList[index];
    if (["--no-visual-corruption", "--no-crash", "--no-device-loss"].includes(argument)) {
      flags.add(argument);
      continue;
    }
    if (!argument.startsWith("--") || index + 1 >= argumentsList.length) {
      throw new Error(`invalid argument: ${argument}`);
    }
    values.set(argument, argumentsList[index + 1]);
    index += 1;
  }

  for (const required of ["--host", "--viewer", "--latency-p95-ms"]) {
    if (!values.has(required)) {
      throw new Error(`missing required argument: ${required}`);
    }
  }
  return { values, flags };
}

async function runCli() {
  const { values, flags } = parseArguments(process.argv.slice(2));
  const [host, viewer] = await Promise.all(
    [values.get("--host"), values.get("--viewer")].map(async (path) =>
      JSON.parse(await readFile(path, "utf8")),
    ),
  );
  const result = evaluateReports(host, viewer, {
    latencyP95Ms: values.get("--latency-p95-ms"),
    noVisualCorruption: flags.has("--no-visual-corruption"),
    noCrash: flags.has("--no-crash"),
    noDeviceLoss: flags.has("--no-device-loss"),
  });
  const payload = `${JSON.stringify(result, null, 2)}\n`;
  const outputPath = values.get("--output");
  if (outputPath) {
    await writeFile(outputPath, payload, "utf8");
  } else {
    process.stdout.write(payload);
  }
  process.exitCode = result.qg1_status === "pass" ? 0 : 1;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  runCli().catch((error) => {
    process.stderr.write(`${error.message}\n`);
    process.exitCode = 2;
  });
}
