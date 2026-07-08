FROM fedora:44@sha256:6c75d5bf57cb0fa5aa4b92c6a83c86c791644496d9ac230de7711f5b8ec3b898 AS builder
# Dockerfile for Intel PCM sensor server
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2020-2024 Intel Corporation

RUN dnf -y install gcc-c++ git findutils make cmake openssl openssl-devel libasan libasan-static hwdata

COPY . /tmp/pcm
WORKDIR /tmp/pcm/build
RUN cmake -DPCM_NO_STATIC_LIBASAN=OFF .. && make -j

FROM fedora:44@sha256:6c75d5bf57cb0fa5aa4b92c6a83c86c791644496d9ac230de7711f5b8ec3b898

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
