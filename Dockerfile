FROM fedora:37@sha256:de153a3928b8901ad05d8d3314a1f7680570979bd2c04c4562b817daa8358a33 as builder

RUN dnf -y install gcc-c++ git findutils make cmake
COPY . /tmp/pcm
RUN cd /tmp/pcm && mkdir build && cd build && cmake .. && make

FROM fedora:37@sha256:de153a3928b8901ad05d8d3314a1f7680570979bd2c04c4562b817daa8358a33
COPY --from=builder /tmp/pcm/build/bin/* /usr/local/bin/
ENV PCM_NO_PERF=1

ENTRYPOINT [ "/usr/local/bin/pcm-sensor-server", "-p", "9738", "-r" ]
