FROM debian:trixie-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        qemu-system-arm qemu-utils qemu-system-gui qemu-system-modules-opengl e2fsprogs \
        python3 python3-venv \
        libegl1 libgl1 libgles2 libgl1-mesa-dri libvirglrenderer1 \
        ca-certificates \
        socat \
        novnc websockify \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/venv
ENV PATH="/opt/venv/bin:$PATH"

COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

WORKDIR /tests
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
