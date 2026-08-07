ARG NODE_BASE=node:24.18.0-trixie-slim@sha256:5301bbf5e8046148348b1dea15436326f43c579031f8d76654a631225bdfe467

FROM --platform=linux/amd64 ${NODE_BASE} AS verify
ARG ESO_VERSION
ARG ESO_ARCHIVE_SHA256
ARG ESO_RELEASE_JSON_SHA256
COPY engine-sim-offline-${ESO_VERSION}.tar /tmp/engine-sim-offline.tar
COPY engine-sim-offline-${ESO_VERSION}.tar.sha256 /tmp/engine-sim-offline.tar.sha256
RUN set -eu; \
    test "$(cat /tmp/engine-sim-offline.tar.sha256)" = "${ESO_ARCHIVE_SHA256}"; \
    test "$(sha256sum /tmp/engine-sim-offline.tar | cut -d ' ' -f 1)" = "${ESO_ARCHIVE_SHA256}"; \
    tar -xf /tmp/engine-sim-offline.tar -C /opt --no-same-owner; \
    root="/opt/engine-sim-offline-${ESO_VERSION}"; \
    manifest="${root}/share/engine-sim-offline/${ESO_VERSION}/release.json"; \
    test "$(sha256sum "${manifest}" | cut -d ' ' -f 1)" = "${ESO_RELEASE_JSON_SHA256}"; \
    test "$(cat "${manifest}.sha256")" = "${ESO_RELEASE_JSON_SHA256}"; \
    test "$("${root}/bin/engine-sim-offline" --version)" = "engine-sim-offline ${ESO_VERSION}"; \
    test "$("${root}/bin/engine-sim-offline-responsive-bake" --version)" = "engine-sim-offline-responsive-bake ${ESO_VERSION}"; \
    "${root}/bin/engine-sim-offline" inspect-ir-catalog --result-format json > /tmp/catalog.json; \
    node -e 'const r=JSON.parse(require("node:fs").readFileSync("/tmp/catalog.json"));if(!r.ok||r.result?.entry_count!==73||r.result?.catalog?.rights_summary?.status!=="MIT")process.exit(1)'; \
    ldd "${root}/bin/engine-sim-offline" > /tmp/cli.ldd; \
    ldd "${root}/libexec/engine-sim-offline/dump-ir-spectrum" > /tmp/helper.ldd; \
    ! grep -q 'not found' /tmp/cli.ldd /tmp/helper.ldd; \
    mv "${root}" /opt/engine-sim-offline

FROM --platform=linux/amd64 ${NODE_BASE} AS runtime
ARG ESO_VERSION
ARG ESO_GIT_SHA
ARG ESO_ARCHIVE_SHA256
ARG ESO_RELEASE_JSON_SHA256
ARG OCI_CREATED
ARG OCI_SOURCE
COPY --from=verify /opt/engine-sim-offline /opt/engine-sim-offline
RUN mkdir -p /work /cache && chown node:node /work /cache
ENV ESO_PREFIX=/opt/engine-sim-offline \
    PATH=/opt/engine-sim-offline/bin:${PATH} \
    TMPDIR=/tmp
WORKDIR /work
USER node
LABEL org.opencontainers.image.title="engine-sim-offline" \
      org.opencontainers.image.description="Deterministic engine rendering, responsive baking, and REVENGINE tooling" \
      org.opencontainers.image.version="${ESO_VERSION}" \
      org.opencontainers.image.revision="${ESO_GIT_SHA}" \
      org.opencontainers.image.created="${OCI_CREATED}" \
      org.opencontainers.image.source="${OCI_SOURCE}" \
      org.opencontainers.image.licenses="LicenseRef-Proprietary" \
      io.engine-sim-offline.archive-sha256="${ESO_ARCHIVE_SHA256}" \
      io.engine-sim-offline.release-json-sha256="${ESO_RELEASE_JSON_SHA256}" \
      io.engine-sim-offline.target="x86_64-linux-gnu"
ENTRYPOINT ["/opt/engine-sim-offline/bin/engine-sim-offline"]
CMD ["--help"]
