// What the programs send (POST /issues, JSON) and the issue made from it: checked here, before GitHub is called.

export type Kind = "bug" | "idea";

export interface Report {
  kind: Kind;
  title: string;
  /** what happened, or the suggestion */
  text: string;
  /** the steps to reproduce, or why it would help (optional) */
  more: string;
  /** "0.5.0" */
  version: string;
  /** "AnyCapture" or "AnyCapture Editor" */
  program: string;
  /** "Windows 11 25H2 (build 26200.1234), pt-BR", or empty when the user left it out */
  system: string;
}

export const LIMITS = { title: 200, text: 8000, more: 4000, version: 32, program: 40, system: 160 } as const;

const PROGRAMS = ["AnyCapture", "AnyCapture Editor"];

/** The report, or the reason it is refused (shown in the program's log, not to the user). */
export function parseReport(raw: unknown): Report | string {
  if (typeof raw !== "object" || raw === null) return "not a JSON object";
  const o = raw as Record<string, unknown>;
  const str = (k: keyof typeof LIMITS): string | null => {
    const v = o[k] ?? "";
    if (typeof v !== "string") return null;
    const t = v.replace(/\r\n/g, "\n").trim();
    return t.length <= LIMITS[k] ? t : null;
  };
  if (o.kind !== "bug" && o.kind !== "idea") return "kind must be bug or idea";
  const r = {
    kind: o.kind,
    title: str("title"),
    text: str("text"),
    more: str("more"),
    version: str("version"),
    program: str("program"),
    system: str("system"),
  };
  for (const [k, v] of Object.entries(r)) {
    if (v === null) return `${k}: not text, or longer than ${LIMITS[k as keyof typeof LIMITS]} characters`;
  }
  if (!r.title || !r.text) return "the title and the description are required";
  if (!/^\d+\.\d+\.\d+([-.+][0-9A-Za-z.-]+)?$/.test(r.version!)) return "version is not a version number";
  if (!PROGRAMS.includes(r.program!)) return "unknown program";
  return r as Report;
}

/** Keeps the reporter's text from pinging people or closing issues on GitHub: @name and "fixes #1" lose their effect. */
export function defuse(s: string): string {
  return s
    .replace(/@(?=[A-Za-z0-9-])/g, "@​")
    .replace(/\b(close[sd]?|fix(e[sd])?|resolve[sd]?)(\s+)#/gi, "$1$3#​");
}

/** The issue: the same headings as the issue forms in .github/ISSUE_TEMPLATE, and the GitHub label. */
export function issueOf(r: Report): { title: string; body: string; labels: string[] } {
  const bug = r.kind === "bug";
  const sections: [string, string][] = [
    [bug ? "What happened?" : "Your suggestion or feedback", r.text],
    [bug ? "Steps to reproduce" : "Why would it help?", r.more],
    ["AnyCapture version", r.version],
  ];
  if (r.system) sections.push(["Windows version", r.system]);
  let body = "";
  for (const [h, t] of sections) {
    body += `### ${h}\n\n${t ? defuse(t) : "_No response_"}\n\n`;
  }
  const menu = bug ? "Help › Report a problem" : "Help › Send a suggestion or feedback";
  body += `_Sent from ${r.program} (${menu}), without a GitHub account._\n`;
  return { title: defuse(r.title), body, labels: [bug ? "bug" : "enhancement"] };
}
