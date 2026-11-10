// Writes secrets.env for `cf deploy --secrets-file`: GITHUB_APP_ID from secrets.env (or the command line) and the
// private key from github-app.pem, on one line. Both files stay out of Git (.gitignore).
//   node secrets.mjs            (GITHUB_APP_ID already in secrets.env)
//   node secrets.mjs 1234567    (the app's ID)
import { existsSync, readFileSync, writeFileSync } from "node:fs";

const env = existsSync("secrets.env") ? readFileSync("secrets.env", "utf8") : "";
const id = process.argv[2] ?? /^GITHUB_APP_ID=(\d+)\s*$/m.exec(env)?.[1];
if (!id) {
  console.error("Put GITHUB_APP_ID=<the app's ID> in secrets.env, or run: node secrets.mjs <ID>");
  process.exit(1);
}
if (!existsSync("github-app.pem")) {
  console.error("Save the app's private key (the .pem GitHub downloads) here as github-app.pem");
  process.exit(1);
}
const pem = readFileSync("github-app.pem", "utf8").trim();
if (!/-----BEGIN (RSA )?PRIVATE KEY-----/.test(pem)) {
  console.error("github-app.pem is not a private key");
  process.exit(1);
}
writeFileSync("secrets.env", `GITHUB_APP_ID=${id}\nGITHUB_APP_PRIVATE_KEY=${pem.replace(/\r?\n/g, "\\n")}\n`);
console.log("secrets.env written (GITHUB_APP_ID and GITHUB_APP_PRIVATE_KEY)");
