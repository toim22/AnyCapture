import { bindings, defineConfig, defineWorker } from "cf/config";

export default defineConfig({
  worker: defineWorker({
    name: "anycapture-issues",
    entrypoint: "./src/index.ts",
    compatibilityDate: "2026-11-01",
    // public address: https://anycapture-issues.<your subdomain>.workers.dev (it goes into snapcore/src/project.rs)
    workersDev: true,
    observability: { enabled: true, logs: { enabled: true, invocationLogs: true } },
    env: {
      GITHUB_REPO: bindings.text("toim22/AnyCapture"),
      // 2 reports a minute per address (the namespace is a number of your choice, unique in the account)
      PER_MINUTE: bindings.rateLimit({ namespace: "4201", simple: { limit: 2, period: 60 } }),
      // written by `npm run first-deploy` from secrets.env (see README.md)
      GITHUB_APP_ID: bindings.secret(),
      GITHUB_APP_PRIVATE_KEY: bindings.secret(),
    },
  }),
});
