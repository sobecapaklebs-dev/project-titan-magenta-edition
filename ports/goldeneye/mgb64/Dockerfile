FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

# Native-port build dependencies plus the matching-target toolchain basics.
RUN apt-get update && apt-get install -y --no-install-recommends \
    bash-completion \
    binutils-mips-linux-gnu \
    build-essential \
    ca-certificates \
    cmake \
    git \
    libcapstone-dev \
    libgl1-mesa-dev \
    libsdl2-dev \
    make \
    pkg-config \
    python3 \
    python3-pil \
    sudo \
    wget \
    && rm -rf /var/lib/apt/lists/*

# qemu-irix is published as an amd64 .deb. Keep non-amd64 Docker builds useful
# for native-port development instead of failing during image creation.
RUN set -eux; \
    arch="$(dpkg --print-architecture)"; \
    if [ "$arch" = "amd64" ]; then \
        wget -O /tmp/qemu-irix.deb \
            https://github.com/n64decomp/qemu-irix/releases/download/v2.11-deb/qemu-irix-2.11.0-2169-g32ab296eef_amd64.deb; \
        dpkg -i /tmp/qemu-irix.deb; \
        rm -f /tmp/qemu-irix.deb; \
    else \
        echo "Skipping qemu-irix install on ${arch}; amd64 package only."; \
    fi

RUN echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers \
    && useradd -ms /bin/bash dev \
    && usermod -aG sudo dev

USER dev
WORKDIR /home/dev/mgb64

CMD ["/bin/bash"]
