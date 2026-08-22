ARG RUNTIME_BASE=debian:bookworm-slim@sha256:abd67ffcfa541b485a3dff59865ab629aa048a6c613e639d36e7456b0b229241

FROM ${RUNTIME_BASE} AS verify
ARG CRANKWAVE_VERSION
ARG CRANKWAVE_ARCHIVE_SHA256
ARG CRANKWAVE_RELEASE_JSON_SHA256
COPY crankwave-${CRANKWAVE_VERSION}.tar /tmp/crankwave.tar
COPY crankwave-${CRANKWAVE_VERSION}.tar.sha256 /tmp/crankwave.tar.sha256
RUN set -eu; \
    test "$(cat /tmp/crankwave.tar.sha256)" = "${CRANKWAVE_ARCHIVE_SHA256}"; \
    test "$(sha256sum /tmp/crankwave.tar | cut -d ' ' -f 1)" = "${CRANKWAVE_ARCHIVE_SHA256}"; \
    tar -xf /tmp/crankwave.tar -C /opt --no-same-owner --no-same-permissions; \
    root="/opt/crankwave-${CRANKWAVE_VERSION}"; \
    manifest="${root}/share/crankwave/${CRANKWAVE_VERSION}/release.json"; \
    test "$(sha256sum "${manifest}" | cut -d ' ' -f 1)" = "${CRANKWAVE_RELEASE_JSON_SHA256}"; \
    test "$(cat "${manifest}.sha256")" = "${CRANKWAVE_RELEASE_JSON_SHA256}"; \
    test "$("${root}/bin/crankwave" --version)" = "crankwave ${CRANKWAVE_VERSION}"; \
    ldd "${root}/bin/crankwave" > /tmp/cli.ldd; \
    ! grep -q 'not found' /tmp/cli.ldd; \
    test ! -e "${root}/bin/crankwave-responsive-bake"; \
    test ! -e "${root}/libexec/crankwave/dump-ir-spectrum"; \
    ! find "${root}" -type f -name '*.wasm' -print -quit | grep -q .; \
    mv "${root}" /opt/crankwave; \
    rm -f /tmp/crankwave.tar /tmp/crankwave.tar.sha256

FROM ${RUNTIME_BASE} AS runtime
ARG CRANKWAVE_VERSION
ARG CRANKWAVE_GIT_SHA
ARG CRANKWAVE_ARCHIVE_SHA256
ARG CRANKWAVE_RELEASE_JSON_SHA256
ARG OCI_CREATED
ARG OCI_SOURCE
ARG SOURCE_DATE_EPOCH
COPY --from=verify /opt/crankwave /opt/crankwave
RUN set -eu; \
    printf '%s\n' 'crankwave:x:65532:' >> /etc/group; \
    printf '%s\n' \
        'crankwave:x:65532:65532:Crankwave:/nonexistent:/usr/sbin/nologin' \
        >> /etc/passwd; \
    mkdir -p /work; \
    chown 65532:65532 /work
ENV CRANKWAVE_PREFIX=/opt/crankwave \
    PATH=/opt/crankwave/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    HOME=/nonexistent \
    TMPDIR=/tmp
WORKDIR /work
USER 65532:65532
LABEL org.opencontainers.image.title="Crankwave" \
      org.opencontainers.image.description="Deterministic engine simulation and responsive audio packaging" \
      org.opencontainers.image.version="${CRANKWAVE_VERSION}" \
      org.opencontainers.image.revision="${CRANKWAVE_GIT_SHA}" \
      org.opencontainers.image.created="${OCI_CREATED}" \
      org.opencontainers.image.source="${OCI_SOURCE}" \
      org.opencontainers.image.licenses="MIT" \
      io.crankwave.archive-sha256="${CRANKWAVE_ARCHIVE_SHA256}" \
      io.crankwave.release-json-sha256="${CRANKWAVE_RELEASE_JSON_SHA256}" \
      io.crankwave.production-runtime="native" \
      io.crankwave.node-required="false" \
      io.crankwave.simulation-wasm-required="false" \
      io.crankwave.target="x86_64-pc-linux-gnu"
ENTRYPOINT ["/opt/crankwave/bin/crankwave"]
CMD ["--help"]
