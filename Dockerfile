FROM fedora:31 as builder

RUN dnf -y install gcc-c++ git findutils make
COPY . /tmp/pcm
RUN cd /tmp/pcm && make

FROM fedora:31
COPY --from=builder /tmp/pcm/*.x /usr/local/bin/

ENTRYPOINT [ "/usr/local/bin/pcm-sensor-server.x", "-p", "9738" ]
