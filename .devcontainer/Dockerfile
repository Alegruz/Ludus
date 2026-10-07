# syntax=docker/dockerfile:1
FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

# The optional CA mount supports HTTPS package mirrors behind a trusted proxy.
# It is used only during the build and never copied into the image.
RUN --mount=type=secret,id=proxy_ca \
    set -eu; \
    if [ -f /run/secrets/proxy_ca ]; then \
        set -- -o Acquire::https::CaInfo=/run/secrets/proxy_ca; \
    fi; \
    apt-get "$@" update && \
    DEBIAN_FRONTEND=noninteractive apt-get "$@" install -y --no-install-recommends \
        build-essential ca-certificates git pkg-config python3 python3-venv \
        clang-18 clang-tools-18 lld-18 clang-format-18 clang-tidy-18 \
        libclang-rt-18-dev && \
    rm -rf /var/lib/apt/lists/*

USER vscode
