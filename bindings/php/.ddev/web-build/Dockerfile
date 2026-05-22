ARG BASE_IMAGE
FROM $BASE_IMAGE

# PHP_VERSION is injected by DDEV (e.g. "8.5") – automatically tracks
# whatever php_version is set to in config.yaml, so no manual update needed.
# The post-start hook contains a dynamic fallback for when this layer is stale.
ARG PHP_VERSION
RUN sudo apt-get update \
  && sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
      -o Dpkg::Options::="--force-confold" \
      build-essential autoconf valgrind \
      "php${PHP_VERSION}-dev" \
  && sudo rm -rf /var/lib/apt/lists/* \
  && echo "Installed phpize: $(phpize${PHP_VERSION} --version 2>&1 | head -1)"
