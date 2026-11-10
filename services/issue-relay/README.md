# anycapture-issues: problem reports without a GitHub account

*Help › Report a problem…* and *Help › Send a suggestion or feedback…* in AnyCapture and the Editor send the form
here. This Cloudflare Worker checks the report, allows at most 2 reports a minute from one address, and creates the
issue on `toim22/AnyCapture` as the **AnyCapture Reports** GitHub App, labeled `bug` or `enhancement`, with the same
headings as the issue forms in `.github/ISSUE_TEMPLATE`. The person reporting needs no GitHub account, and no key
ships inside the programs: the app's private key lives only in the Worker's secrets.

When the Worker cannot be reached, the programs fall back to opening GitHub's issue form in the browser, filled in
(that path needs a GitHub account).

- `src/index.ts`: `POST /issues` (JSON) → `201 {"url": …}`; 400 refused, 413 too large, 429 too many, 502 GitHub failed.
  `GET /` answers `ok`.
- `src/report.ts`: the checks (kind, required title and description, lengths, version, program) and the issue's text.
  `@name` mentions and "fixes #1" are defused, so a report cannot ping people or close issues.
- `src/github.ts`: the GitHub App sign-in (a JWT signed with the private key → a one-hour installation token, kept
  while valid) and the issue creation.
- `test.ts`: offline tests; `npm test`, and every deploy runs them first.

## Set up (once)

### 1. Create the GitHub App

1. Open <https://github.com/settings/apps/new> (signed in as `toim22`).
2. **GitHub App name**: `AnyCapture Reports` (any free name works; it shows on the issues as `<name>[bot]`).
3. **Homepage URL**: `https://github.com/toim22/AnyCapture`.
4. **Webhook**: untick **Active** (the app receives nothing).
5. **Permissions › Repository permissions › Issues**: **Read and write**. Leave everything else as it is
   (*Metadata: Read-only* is added by itself).
6. **Where can this GitHub App be installed?** **Only on this account**.
7. **Create GitHub App**.

### 2. Get its ID and private key

1. On the app's page, copy the **App ID** (a number, under *About*).
2. At the bottom, **Private keys › Generate a private key**. GitHub downloads a `.pem` file.
3. Save it in this folder as `github-app.pem`. It never goes into Git (`.gitignore`).

### 3. Install it on the repository

1. In the app's settings, left menu, **Install App** → **Install** next to `toim22`.
2. **Only select repositories** → `AnyCapture` → **Install**.

### 4. Publish the Worker

In this folder (`services/issue-relay`):

1. Copy `secrets.example.env` to `secrets.env` and put the App ID after `GITHUB_APP_ID=`.
2. Run:

```bash
npm.cmd install
```

```bash
npm.cmd run first-deploy
```

`first-deploy` runs the tests, adds the private key to `secrets.env` (`secrets.mjs`), then publishes with
`cf deploy --secrets-file secrets.env` (the first time, `cf` asks you to sign in to Cloudflare in the browser). At the
end it prints the address, `https://anycapture-issues.<your subdomain>.workers.dev`.

3. Check it: opening that address in the browser shows `ok`.
4. Delete `secrets.env` and `github-app.pem` (or keep the `.pem` somewhere safe outside the repository): the keys are
   stored at Cloudflare now.

### 5. Point the programs at it

Put the address in `RELAY_URL` in `rust/snapcore/src/project.rs` and build again (`build.bat`). To try a Worker
somewhere else without rebuilding, set the environment variable `ANYCAPTURE_ISSUE_RELAY` to its address before
starting AnyCapture.

## Every day

- Changed the code? `npm.cmd run deploy` (tests first).
- Logs: Cloudflare dashboard › Workers & Pages › `anycapture-issues` › Logs (each created issue and every GitHub error).
- A new private key (the old one leaked or was deleted): generate it on the app's page, save it as `github-app.pem`, put
  the ID in `secrets.env` again and run `npm.cmd run first-deploy`.
- Spam: close or delete the issue on GitHub. To stop all reports at once, uninstall the app from the repository
  (GitHub › Settings › Applications › AnyCapture Reports › Configure › Uninstall); the programs then open the browser
  form instead.

## Cost

Free: the Workers free plan covers 100,000 requests a day, and the rate limiting binding is included.
