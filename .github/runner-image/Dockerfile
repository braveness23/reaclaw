FROM ghcr.io/actions/actions-runner:latest

# Pre-install the full ReaClaw build toolchain so CI jobs don't pay the
# apt-get update + install cost on every run. Mirrors how omega does it
# (ghcr.io/braveness23/omega-ci-runner) but with ReaClaw's deps: OpenSSL for the
# embedded HTTPS server, and curl/unzip/git/tar for the vendored-dep fetch.
USER root

# ccache: shared with local dev via scripts/checks/build.sh (auto-detected,
# opt-in there); here it's always present so CI gets it automatically. CI
# persists ~/.ccache across runs via actions/cache (see ci.yml).
#
# xvfb/jackd2/lib*: the e2e-render-smoke job's headless REAPER dependencies
# (Xvfb display, dummy JACK backend, REAPER's own runtime shared-lib deps).
# Baked in here for the same reason as the build toolchain — these packages
# are static across runs, so jobs shouldn't pay the apt-get cost every time.
# The REAPER *binary* itself stays pinned/downloaded per-run in ci.yml
# (REAPER_VERSION) rather than baked in, to avoid duplicating that version
# pin in a second place.
# jackd2's postinst prompts for realtime-priority tuning; answer it up front
# so the build doesn't hang (same debconf answer ci.yml used to set per-run).
RUN echo "jackd jackd/tweak_rt_limits boolean false" | debconf-set-selections

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    clang-format-18 \
    cmake \
    ninja-build \
    ccache \
    libssl-dev \
    git \
    curl \
    ca-certificates \
    unzip \
    tar \
    xvfb \
    jackd2 \
    python3 \
    libx11-6 libxi6 libxext6 libxcb1 libfontconfig1 libfreetype6 \
    libgl1 libasound2t64 libgtk-3-0t64 libpulse0 \
    && rm -rf /var/lib/apt/lists/*

USER runner
