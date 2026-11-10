// anycapture-issues: lets AnyCapture's Help › Report a problem / Send a suggestion or feedback file a GitHub issue
// without the user having a GitHub account. The programs POST the report here; the Worker checks it, limits how often
// one address may send, and creates the issue as the AnyCapture Reports GitHub App (labels bug or enhancement).
//
//   POST /issues   {"kind":"bug"|"idea","title","text","more","version","program","system"}
//                  → 201 {"url":"https://github.com/toim22/AnyCapture/issues/12"}
//                  → 400 {"error":"…"} (refused), 429 (too many), 502 (GitHub failed: the program opens the browser)
//   GET  /         → 200 "ok" (a check that it is up)

import { createIssue } from "./github.ts";
import { issueOf, parseReport } from "./report.ts";

interface RateLimiter {
  limit(o: { key: string }): Promise<{ success: boolean }>;
}

export interface Env {
  /** the GitHub App's ID (GitHub › Settings › Developer settings › GitHub Apps › the app › About) */
  GITHUB_APP_ID: string;
  /** the app's private key (.pem) */
  GITHUB_APP_PRIVATE_KEY: string;
  /** owner/name; set in cloudflare.config.ts */
  GITHUB_REPO: string;
  /** 2 reports a minute per address */
  PER_MINUTE: RateLimiter;
}

const MAX_BODY = 32 * 1024;

function json(status: number, value: object): Response {
  return new Response(JSON.stringify(value), { status, headers: { "Content-Type": "application/json; charset=utf-8" } });
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);
    if (request.method === "GET" && url.pathname === "/") return new Response("ok");
    if (url.pathname !== "/issues") return json(404, { error: "not found" });
    if (request.method !== "POST") return json(405, { error: "POST only" });

    const ip = request.headers.get("CF-Connecting-IP") ?? "unknown";
    if (!(await env.PER_MINUTE.limit({ key: ip })).success) return json(429, { error: "too many reports, try again in a minute" });

    const length = Number(request.headers.get("Content-Length") ?? "0");
    if (length > MAX_BODY) return json(413, { error: "too large" });
    const text = await request.text();
    if (text.length > MAX_BODY) return json(413, { error: "too large" });
    let raw: unknown;
    try {
      raw = JSON.parse(text);
    } catch {
      return json(400, { error: "not JSON" });
    }
    const report = parseReport(raw);
    if (typeof report === "string") return json(400, { error: report });

    try {
      const issue = await createIssue({ appId: env.GITHUB_APP_ID, privateKey: env.GITHUB_APP_PRIVATE_KEY, repo: env.GITHUB_REPO },
                                      issueOf(report));
      console.log(`issue created: ${issue} (${report.kind}, ${report.program} ${report.version})`);
      return json(201, { url: issue });
    } catch (e) {
      console.error(`GitHub refused the report: ${e instanceof Error ? e.message : String(e)}`);
      return json(502, { error: "GitHub could not take the report now" });
    }
  },
};
