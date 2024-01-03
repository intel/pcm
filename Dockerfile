FROM fedora:39@sha256:06df381d697d14940c886fda8e94a4fdc838df74e93f65111ed3ea04f7a7d6e0 as builder

RUN dnf -y install gcc-c++ git findutils make cmake
COPY . /tmp/pcm
RUN cd /tmp/pcm && mkdir build && cd build && cmake .. && make

FROM fedora:39@sha256:06df381d697d14940c886fda8e94a4fdc838df74e93f65111ed3ea04f7a7d6e0
COPY --from=builder /tmp/pcm/build/bin/* /usr/local/bin/
ENV PCM_NO_PERF=1

ENTRYPOINT [ "/usr/local/bin/pcm-sensor-server", "-p", "9738", "-r" ]
