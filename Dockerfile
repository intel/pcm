FROM fedora:40@sha256:d0207dbb078ee261852590b9a8f1ab1f8320547be79a2f39af9f3d23db33735e AS builder
# Dockerfile for Intel PCM sensor server
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2020-2024 Intel Corporation

RUN dnf -y install gcc-c++ git findutils make cmake openssl openssl-devel libasan libasan-static
COPY . /tmp/pcm
RUN cd /tmp/pcm && mkdir build && cd build && cmake .. && make -j

FROM fedora:40@sha256:d0207dbb078ee261852590b9a8f1ab1f8320547be79a2f39af9f3d23db33735e
COPY --from=builder /tmp/pcm/build/bin/* /usr/local/bin/
ENV PCM_NO_PERF=1

ENTRYPOINT [ "/usr/local/bin/pcm-sensor-server", "-p", "9738", "-r" ]
