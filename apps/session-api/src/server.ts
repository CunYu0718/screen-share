import { buildApp } from "./app.js";

const host = process.env.SCREEN_SHARE_API_HOST ?? "127.0.0.1";
const port = Number.parseInt(process.env.SCREEN_SHARE_API_PORT ?? "8080", 10);

if (!Number.isInteger(port) || port < 1 || port > 65_535) {
  throw new Error("SCREEN_SHARE_API_PORT must be an integer between 1 and 65535");
}

const app = buildApp({ logger: true });

const shutdown = async (signal: NodeJS.Signals): Promise<void> => {
  app.log.info({ signal }, "shutdown requested");
  await app.close();
};

for (const signal of ["SIGINT", "SIGTERM"] as const) {
  process.once(signal, () => {
    void shutdown(signal);
  });
}

try {
  await app.listen({ host, port });
} catch (error: unknown) {
  app.log.error({ error }, "server failed to start");
  process.exitCode = 1;
}
