# OCI image

The GHCR image is a generic Linux/amd64 Engine Sim Offline job image. It is built
only from the already verified, manifest-bound release tar; it never rebuilds ESO.
The release workflow pins the outer tar SHA-256 and inner `release.json` SHA-256 as
both build checks and OCI labels.

The native CLI is the default entrypoint:

```sh
docker run --rm ghcr.io/svetlozarvalchev/engine-sim-offline@sha256:<digest> --version
```

Mount job inputs and outputs beneath `/work`. Mount a persistent responsive-bake
cache at `/cache` when desired. The image runs as the upstream `node` image's
unprivileged `node` user; `/opt/engine-sim-offline` is read-only product content.

The responsive baker remains its own installed command:

```sh
docker run --rm \
  --entrypoint /opt/engine-sim-offline/bin/engine-sim-offline-responsive-bake \
  ghcr.io/svetlozarvalchev/engine-sim-offline@sha256:<digest> \
  --help
```

Tags are discovery aids only. Integrations must pin the OCI manifest digest and
retain the inner ESO release binding reported in the corresponding GitHub Release.
