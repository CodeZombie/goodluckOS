FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        qemu-system-arm qemu-utils e2fsprogs \
        python3 python3-venv python3-pip \
        ca-certificates \
        socat \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/venv
ENV PATH="/opt/venv/bin:$PATH"

COPY requirements.txt /tmp/requirements.txt
RUN pip install --no-cache-dir -r /tmp/requirements.txt

COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

WORKDIR /tests
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
