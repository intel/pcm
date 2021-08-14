FROM redhat/ubi8 as builder

RUN dnf -y install gcc-c++ git findutils make
COPY . /tmp/pcm
RUN cd /tmp/pcm && make

FROM redhat/ubi8
COPY --from=builder /tmp/pcm/*.x /usr/local/bin/
ENV PCM_NO_PERF=1

ENTRYPOINT [ "/usr/local/bin/pcm-sensor-server.x", "-p", "9738", "-r" ]
