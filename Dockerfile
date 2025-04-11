FROM fedora:41@sha256:f84a7b765ce09163d11de44452a4b56c1b2f5571b6f640b3b973c6afc4e63212 AS builder
# Dockerfile for Intel PCM sensor server
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2020-2024 Intel Corporation

RUN dnf -y install gcc-c++ git findutils make cmake openssl openssl-devel libasan libasan-static hwdata
COPY . /tmp/pcm
RUN cd /tmp/pcm && mkdir build && cd build && cmake -DPCM_NO_STATIC_LIBASAN=OFF .. && make -j

FROM fedora:41@sha256:f84a7b765ce09163d11de44452a4b56c1b2f5571b6f640b3b973c6afc4e63212
COPY --from=builder /tmp/pcm/build/bin/* /usr/local/bin/
COPY --from=builder /tmp/pcm/build/bin/opCode*.txt /usr/local/share/pcm/
COPY --from=builder /usr/share/hwdata/pci.ids /usr/share/hwdata/pci.ids
ENV PCM_NO_PERF=1

ENTRYPOINT [ "/usr/local/bin/pcm-sensor-server", "-p", "9738", "-r" ]
