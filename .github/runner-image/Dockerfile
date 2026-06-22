FROM ghcr.io/actions/actions-runner:latest

# Pre-install the full ReaClaw build toolchain so CI jobs don't pay the
# apt-get update + install cost on every run. Mirrors how omega does it
# (ghcr.io/braveness23/omega-ci-runner) but with ReaClaw's deps: OpenSSL for the
# embedded HTTPS server, and curl/unzip/git/tar for the vendored-dep fetch.
USER root

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    clang-format-18 \
    cmake \
    ninja-build \
    libssl-dev \
    git \
    curl \
    ca-certificates \
    unzip \
    tar \
    && rm -rf /var/lib/apt/lists/*

USER runner
