# OCI image

The GHCR image is a generic Linux/amd64 Crankwave job image. It is
assembled from the already verified, manifest-bound release tar and never rebuilds
the product. The release workflow binds the source revision, outer tar SHA-256, and inner
`release.json` SHA-256 into OCI labels and the GitHub release binding.

The image is based on a digest-pinned Debian slim runtime. It contains no Node.js
runtime and no simulation WebAssembly module. The one native CLI is its direct
entrypoint:

```sh
docker run --rm \
  ghcr.io/svetlozarvalchev/crankwave@sha256:<digest> \
  --version
```

Mount inputs read-only and a caller-owned writable directory at `/work`:

```sh
docker run --rm \
  --mount type=bind,src="$PWD/engine.json",dst=/inputs/engine.json,readonly \
  --mount type=bind,src="$PWD/output",dst=/work \
  ghcr.io/svetlozarvalchev/crankwave@sha256:<digest> \
  bake-crankwave \
  --engine /inputs/engine.json \
  --output /work/engine.crankwave \
  --result-format json
```

The default process is the fixed unprivileged user `65532:65532` and
`/opt/crankwave` is immutable product content. The image is not AWS-specific;
job scheduling, storage mounts, deadlines, and cancellation remain adapter policy.

Tags are discovery aids only. Production integrations pin the OCI manifest digest
and retain the matching semantic release, distribution SHA-256, and `release.json`
SHA-256 from the GitHub release binding.
