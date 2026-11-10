// Tests without network access and without GitHub. Use: npm test (deploy runs them first).
import assert from "node:assert/strict";
import { generateKeyPairSync } from "node:crypto";
import { pemToPkcs8 } from "./src/github.ts";
import { defuse, issueOf, parseReport, type Report } from "./src/report.ts";

const good = { kind: "bug", title: "Crash", text: "It closed", more: "", version: "0.5.0", program: "AnyCapture", system: "" };

// 1) What is refused before GitHub is called
{
  assert.equal(typeof parseReport(good), "object");
  assert.match(parseReport({ ...good, kind: "spam" }) as string, /kind/);
  assert.match(parseReport({ ...good, title: "  " }) as string, /required/);
  assert.match(parseReport({ ...good, text: "x".repeat(8001) }) as string, /text/);
  assert.match(parseReport({ ...good, version: "latest" }) as string, /version/);
  assert.match(parseReport({ ...good, program: "Other" }) as string, /program/);
  assert.match(parseReport({ ...good, title: 5 }) as string, /title/);
  assert.equal(typeof parseReport({ ...good, version: "0.5.0-beta.1" }), "object");
  assert.equal(typeof parseReport("text"), "string");
}

// 2) The issue: the form's headings, "_No response_" for an empty field, the label
{
  const bug = issueOf(parseReport({ ...good, more: "1. Open", system: "Windows 11 25H2 (build 26200.1), pt-BR" }) as Report);
  assert.deepEqual(bug.labels, ["bug"]);
  assert.ok(bug.body.startsWith("### What happened?\n\nIt closed\n\n### Steps to reproduce\n\n1. Open\n\n### AnyCapture version\n\n0.5.0\n\n### Windows version\n\nWindows 11"), bug.body);
  const idea = issueOf(parseReport({ ...good, kind: "idea", program: "AnyCapture Editor" }) as Report);
  assert.deepEqual(idea.labels, ["enhancement"]);
  assert.ok(idea.body.startsWith("### Your suggestion or feedback\n\nIt closed\n\n### Why would it help?\n\n_No response_\n\n"), idea.body);
  assert.ok(!idea.body.includes("Windows version"));
  assert.ok(idea.body.endsWith("_Sent from AnyCapture Editor (Help › Send a suggestion or feedback), without a GitHub account._\n"));
}

// 3) Mentions and closing keywords lose their effect
{
  assert.equal(defuse("ask @toim22"), "ask @​toim22");
  assert.equal(defuse("this fixes #3"), "this fixes #​3");
  assert.equal(defuse("e-mail a@b.com"), "e-mail a@​b.com");
}

// 4) The private key GitHub gives (PKCS#1) signs in WebCrypto after the conversion, also on one line with "\n"
{
  const { privateKey, publicKey } = generateKeyPairSync("rsa", { modulusLength: 2048 });
  const pem = privateKey.export({ type: "pkcs1", format: "pem" }).toString();
  assert.match(pem, /BEGIN RSA PRIVATE KEY/);
  for (const text of [pem, pem.trim().replace(/\r?\n/g, "\\n")]) {
    const key = await crypto.subtle.importKey("pkcs8", pemToPkcs8(text), { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" }, false, ["sign"]);
    const data = new TextEncoder().encode("header.payload");
    const sig = await crypto.subtle.sign("RSASSA-PKCS1-v1_5", key, data);
    const pub = await crypto.subtle.importKey("spki", publicKey.export({ type: "spki", format: "der" }),
                                              { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" }, false, ["verify"]);
    assert.ok(await crypto.subtle.verify("RSASSA-PKCS1-v1_5", pub, sig, data));
  }
  const pkcs8 = privateKey.export({ type: "pkcs8", format: "pem" }).toString();
  await crypto.subtle.importKey("pkcs8", pemToPkcs8(pkcs8), { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" }, false, ["sign"]);
}

console.log("relay tests: ok");
