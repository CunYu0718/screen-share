import Fastify, { LogController, type FastifyInstance, type FastifyServerOptions } from "fastify";

import { capabilities } from "./capabilities.js";
import { errorCodes, type ErrorResponse } from "./contracts.js";

function isValidationError(error: unknown): boolean {
  return typeof error === "object" && error !== null && "validation" in error;
}

export interface BuildAppOptions {
  logger?: FastifyServerOptions["logger"];
}

export function buildApp(options: BuildAppOptions = {}): FastifyInstance {
  const app = Fastify({
    logController: new LogController({ disableRequestLogging: true }),
    logger: options.logger ?? false,
    requestIdHeader: "x-request-id",
  });

  app.get("/healthz", async () => ({
    service: "session-api",
    status: "ok",
    version: "0.1.0",
  }));

  app.get("/v1/capabilities", async () => capabilities);

  app.setNotFoundHandler(async (request, reply) => {
    const response: ErrorResponse = {
      error: {
        code: errorCodes.routeNotFound,
        message: "Route not found",
        requestId: request.id,
      },
    };

    await reply.status(404).send(response);
  });

  app.setErrorHandler(async (error, request, reply) => {
    request.log.error({ error, requestId: request.id }, "request failed");
    const validationError = isValidationError(error);

    const response: ErrorResponse = {
      error: {
        code: validationError ? errorCodes.invalidRequest : errorCodes.internalError,
        message: validationError ? "Invalid request" : "Internal server error",
        requestId: request.id,
      },
    };

    await reply.status(validationError ? 400 : 500).send(response);
  });

  return app;
}
