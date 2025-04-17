FROM fedora:42@sha256:ee88ab8a5c8bf78687ddcecadf824767e845adc19d8cdedb56f48521eb162b43 AS builder
# Dockerfile for Intel PCM sensor server
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2020-2024 Intel Corporation

RUN dnf -y install gcc-c++ git findutils make cmake openssl openssl-devel libasan libasan-static hwdata
COPY . /tmp/pcm
RUN cd /tmp/pcm && mkdir build && cd build && cmake -DPCM_NO_STATIC_LIBASAN=OFF .. && make -j

FROM fedora:42@sha256:ee88ab8a5c8bf78687ddcecadf824767e845adc19d8cdedb56f48521eb162b43
COPY --from=builder /tmp/pcm/build/bin/* /usr/local/bin/
COPY --from=builder /tmp/pcm/build/bin/opCode*.txt /usr/local/share/pcm/
COPY --from=builder /usr/share/hwdata/pci.ids /usr/share/hwdata/pci.ids
ENV PCM_NO_PERF=1

ENTRYPOINT [ "/usr/local/bin/pcm-sensor-server", "-p", "9738", "-r" ]
