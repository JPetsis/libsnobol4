# Install PIE (PHP Installer for Extensions) into the web image.
#
# Pinned to a specific release; the PHAR's sha256 is checked and its Sigstore
# attestation is verified against the php/pie repository (public API, no auth).
# See https://github.com/php/pie#installing-pie
#
# Update PIE_VERSION + PIE_SHA256 when bumping.  Verify locally with:
#   gh attestation verify --owner php pie.phar
#   shasum -a 256 pie.phar

ARG PIE_VERSION=1.4.9
ARG PIE_SHA256=19a31ddd4bfd08b9eb5eaad2e5f63e76e7919cae7683852da41c80da704ad6c0

RUN export DEBIAN_FRONTEND=noninteractive \
 && apt-get update \
 && apt-get install -y --no-install-recommends curl unzip \
 && curl -fL --retry 3 -o /tmp/pie.phar \
      "https://github.com/php/pie/releases/download/${PIE_VERSION}/pie.phar" \
 && echo "${PIE_SHA256}  /tmp/pie.phar" | sha256sum -c - \
 && curl -fsSL "https://api.github.com/repos/php/pie/attestations/sha256:${PIE_SHA256}" \
      | grep -q '"attestations": \[' \
 && install -m 0755 /tmp/pie.phar /usr/local/bin/pie \
 && rm -f /tmp/pie.phar \
 && rm -rf /var/lib/apt/lists/*
