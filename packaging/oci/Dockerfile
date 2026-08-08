ARG RUNTIME_BASE=debian:bookworm-slim@sha256:abd67ffcfa541b485a3dff59865ab629aa048a6c613e639d36e7456b0b229241

FROM ${RUNTIME_BASE} AS verify
ARG ESO_VERSION
ARG ESO_ARCHIVE_SHA256
ARG ESO_RELEASE_JSON_SHA256
COPY engine-sim-offline-${ESO_VERSION}.tar /tmp/engine-sim-offline.tar
COPY engine-sim-offline-${ESO_VERSION}.tar.sha256 /tmp/engine-sim-offline.tar.sha256
RUN set -eu; \
    test "$(cat /tmp/engine-sim-offline.tar.sha256)" = "${ESO_ARCHIVE_SHA256}"; \
    test "$(sha256sum /tmp/engine-sim-offline.tar | cut -d ' ' -f 1)" = "${ESO_ARCHIVE_SHA256}"; \
    tar -xf /tmp/engine-sim-offline.tar -C /opt --no-same-owner --no-same-permissions; \
    root="/opt/engine-sim-offline-${ESO_VERSION}"; \
    manifest="${root}/share/engine-sim-offline/${ESO_VERSION}/release.json"; \
    test "$(sha256sum "${manifest}" | cut -d ' ' -f 1)" = "${ESO_RELEASE_JSON_SHA256}"; \
    test "$(cat "${manifest}.sha256")" = "${ESO_RELEASE_JSON_SHA256}"; \
    test "$("${root}/bin/engine-sim-offline" --version)" = "engine-sim-offline ${ESO_VERSION}"; \
    ldd "${root}/bin/engine-sim-offline" > /tmp/cli.ldd; \
    ! grep -q 'not found' /tmp/cli.ldd; \
    test ! -e "${root}/bin/engine-sim-offline-responsive-bake"; \
    test ! -e "${root}/libexec/engine-sim-offline/dump-ir-spectrum"; \
    ! find "${root}" -type f -name '*.wasm' -print -quit | grep -q .; \
    mv "${root}" /opt/engine-sim-offline; \
    rm -f /tmp/engine-sim-offline.tar /tmp/engine-sim-offline.tar.sha256

FROM ${RUNTIME_BASE} AS runtime
ARG ESO_VERSION
ARG ESO_GIT_SHA
ARG ESO_ARCHIVE_SHA256
ARG ESO_RELEASE_JSON_SHA256
ARG OCI_CREATED
ARG OCI_SOURCE
ARG SOURCE_DATE_EPOCH
COPY --from=verify /opt/engine-sim-offline /opt/engine-sim-offline
RUN set -eu; \
    printf '%s\n' 'engine-sim-offline:x:65532:' >> /etc/group; \
    printf '%s\n' \
        'engine-sim-offline:x:65532:65532:Engine Sim Offline:/nonexistent:/usr/sbin/nologin' \
        >> /etc/passwd; \
    mkdir -p /work; \
    chown 65532:65532 /work
ENV ESO_PREFIX=/opt/engine-sim-offline \
    PATH=/opt/engine-sim-offline/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    HOME=/nonexistent \
    TMPDIR=/tmp
WORKDIR /work
USER 65532:65532
LABEL org.opencontainers.image.title="engine-sim-offline" \
      org.opencontainers.image.description="Native deterministic engine audition and REVENGINE generation" \
      org.opencontainers.image.version="${ESO_VERSION}" \
      org.opencontainers.image.revision="${ESO_GIT_SHA}" \
      org.opencontainers.image.created="${OCI_CREATED}" \
      org.opencontainers.image.source="${OCI_SOURCE}" \
      org.opencontainers.image.licenses="LicenseRef-Proprietary" \
      io.engine-sim-offline.archive-sha256="${ESO_ARCHIVE_SHA256}" \
      io.engine-sim-offline.release-json-sha256="${ESO_RELEASE_JSON_SHA256}" \
      io.engine-sim-offline.production-runtime="native" \
      io.engine-sim-offline.node-required="false" \
      io.engine-sim-offline.simulation-wasm-required="false" \
      io.engine-sim-offline.target="x86_64-pc-linux-gnu"
ENTRYPOINT ["/opt/engine-sim-offline/bin/engine-sim-offline"]
CMD ["--help"]
