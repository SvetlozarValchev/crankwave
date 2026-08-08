ARG BUILDER_BASE_IMAGE=gcc:12.4.0-bookworm@sha256:fa4be2f0a762fb14cc745289227d29aa32f0b55057c301d0d61a5eba46c85d99
ARG NODE_BASE_IMAGE=node:24.18.0-bookworm-slim@sha256:6f7b03f7c2c8e2e784dcf9295400527b9b1270fd37b7e9a7285cf83b6951452d

FROM ${NODE_BASE_IMAGE} AS ci_node
FROM ${BUILDER_BASE_IMAGE}

ARG CMAKE_DEBIAN_VERSION=3.25.1-1
ARG GIT_LFS_DEBIAN_VERSION=3.3.0-1+deb12u1
ARG NINJA_DEBIAN_VERSION=1.11.1-2~deb12u1

RUN set -eu; \
    apt-get update; \
    DEBIAN_FRONTEND=noninteractive apt-get install --yes --no-install-recommends \
        "cmake=${CMAKE_DEBIAN_VERSION}" \
        "git-lfs=${GIT_LFS_DEBIAN_VERSION}" \
        "ninja-build=${NINJA_DEBIAN_VERSION}"; \
    rm -rf /var/lib/apt/lists/*

# Node is a test/manifest-verification tool in this builder only. It never enters
# the installed prefix, release tar, or production OCI image.
COPY --from=ci_node /usr/local/ /usr/local/

ENV LANG=C \
    LC_ALL=C \
    TZ=UTC
