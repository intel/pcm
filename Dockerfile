FROM fedora:40@sha256:4e007f288dce23966216be81ef62ba05d139b9338f327c1d1c73b7167dd47312 as builder

RUN dnf -y install gcc-c++ git findutils make cmake openssl openssl-devel
COPY . /tmp/pcm
RUN cd /tmp/pcm && mkdir build && cd build && cmake .. && make -j

FROM fedora:40@sha256:4e007f288dce23966216be81ef62ba05d139b9338f327c1d1c73b7167dd47312
COPY --from=builder /tmp/pcm/build/bin/* /usr/local/bin/
ENV PCM_NO_PERF=1

ENTRYPOINT [ "/usr/local/bin/pcm-sensor-server", "-p", "9738", "-r" ]
