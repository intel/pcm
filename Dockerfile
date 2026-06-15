FROM fedora:43@sha256:781b7642e8bf256e9cf75d2aa58d86f5cc695fd2df113517614e181a5eee9138 AS builder
# Dockerfile for Intel PCM sensor server
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2020-2024 Intel Corporation

RUN dnf -y install gcc-c++ git findutils make cmake openssl openssl-devel libasan libasan-static hwdata

COPY . /tmp/pcm
WORKDIR /tmp/pcm/build
RUN cmake -DPCM_NO_STATIC_LIBASAN=OFF .. && make -j

FROM fedora:43@sha256:781b7642e8bf256e9cf75d2aa58d86f5cc695fd2df113517614e181a5eee9138

COPY --from=builder /tmp/pcm/build/bin/* /usr/local/bin/
COPY --from=builder /tmp/pcm/build/bin/opCode*.txt /usr/local/share/pcm/
COPY --from=builder /usr/share/hwdata/pci.ids /usr/share/hwdata/pci.ids
ENV PCM_NO_PERF=1

RUN useradd -m pcm-user

# Allow pcm-user to run the server via sudo without a password
RUN echo "pcm-user ALL=(root) NOPASSWD: /usr/local/bin/pcm-sensor-server" >> /etc/sudoers

USER pcm-user

HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
  CMD sudo /usr/local/bin/pcm-sensor-server --help > /dev/null 2>&1 || exit 1

ENTRYPOINT [ "sudo", "/usr/local/bin/pcm-sensor-server", "-p", "9738", "-r" ]
