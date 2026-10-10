# Deploying the shared demo

RoverOps runs as one native C++ process inside Docker. It serves the compiled React
assets, REST API, and WebSocket. It needs no database, secrets, or persistent disk.

## Render Free

1. Sign in to Render with GitHub.
2. [Create a Blueprint from this repository](https://render.com/deploy?repo=https://github.com/adamxrodriguez/RoverOps).
3. Confirm the `roverops` web service uses the **Free** compute plan, then deploy.
4. Open the service's assigned HTTPS URL after the health check succeeds.

The Blueprint selects Docker, the `main` branch, port 8080, `/api/health`, and
automatic deployment after CI passes. `deploy/start.sh` uses Render's supplied
`RENDER_EXTERNAL_URL` as the exact allowed browser origin. A custom domain requires
setting `PUBLIC_ORIGIN` to that exact HTTPS origin, with no trailing slash.

If creating the service manually, choose New Web Service, this GitHub repository,
Docker runtime, **Free**, and `/api/health` as the health-check path. Leave the Docker
command blank so the image's entrypoint runs. Set auto-deploy to after CI checks pass.

[Free service limits](https://render.com/docs/free) include sleeping after 15 minutes
without inbound traffic, roughly one minute to wake, and monthly usage quotas.
A paid subscription is unnecessary for a small demo. An always-awake service would
require paid compute. This simulator keeps state in memory, so sleep, restart, and
deploy reset its rover and battery.

## Local production-container check

```sh
docker build -t roverops .
docker run --rm --name roverops -p 8080:8080 \
  -e PUBLIC_ORIGIN=http://127.0.0.1:8080 roverops
```

Open `http://127.0.0.1:8080`. The container runs as an unprivileged user. Its C++ build
and unit tests run during image creation. The deployment entrypoint binds
`0.0.0.0:$PORT`, explicitly enables public-demo mode, and mounts only `dashboard/dist`.

To run browser acceptance tests against this container or your own deployed demo:

```sh
cd dashboard
npm ci
npx playwright install chromium
ROVEROPS_BASE_URL=http://127.0.0.1:8080 npm run test:e2e
```

These tests issue real rover commands. Run them when nobody else is using the demo.
GitHub Actions also builds the production image and exercises it in Chromium.

## Operation and limits

All visitors share one simulated rover and can command it without signing in.
The UI labels this explicitly. There is no hardware connection, user data, or secret
in the service. Origin filtering prevents commands from unapproved browser origins;
it does not prevent direct API use. The default limit is four telemetry viewers.

Use Render's deployment logs for build/runtime failures and `/api/health` for
readiness. To recover a failed release, use Render's Rollback action for a known
successful deployment, or revert the commit on `main` and let CI trigger a deploy.
Every restart resets simulation state. Suspending the service stops the public demo.
