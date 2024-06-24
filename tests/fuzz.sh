
export PCM_ENFORCE_MBM="1"

factor=100

CC=`which clang` CXX=`which clang++` cmake ..  -DCMAKE_BUILD_TYPE=Debug -DFUZZ=1 && mkdir -p corpus &&
make urltest-fuzz \
     pcm-fuzz \
     pcm-memory-fuzz \
     pcm-sensor-server-fuzz \
     -j &&
rm -rf corpus/* &&
printf "GET / HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/1 &&
printf "GET /metrics HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/2 &&
printf "GET /persecond HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/3 &&
printf "GET /persecond HTTP/1.1\r\nHost: localhost\r\nAccept: application/json\r\n\r\n" > corpus/3.1 &&
printf "GET /persecond HTTP/1.1\r\nHost: localhost\r\nAccept: text/plain; version=0.0.4\r\n\r\n" > corpus/3.2 &&
printf "GET /persecond/1 HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n"   > corpus/4 &&
printf "GET /persecond/1 HTTP/1.1\r\nHost: localhost\r\nAccept: application/json\r\n\r\n"   > corpus/4.1 &&
printf "GET /persecond/1 HTTP/1.1\r\nHost: localhost\r\nAccept: text/plain; version=0.0.4\r\n\r\n"   > corpus/4.2 &&
printf "GET /persecond/10 HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/5 &&
printf "GET /persecond/10 HTTP/1.1\r\nHost: localhost\r\nAccept: application/json\r\n\r\n" > corpus/5.1 &&
printf "GET /persecond/10 HTTP/1.1\r\nHost: localhost\r\nAccept: text/plain; version=0.0.4\r\n\r\n" > corpus/5.2 &&
printf "GET /persecond/100 HTTP/1.1\r\nHost: localhost\r\nAccept: application/json\r\n\r\n" > corpus/6 &&
printf "GET /metrics HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/7 &&
printf "GET /dashboard/influxdb HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/8 &&
printf "GET /dashboard/prometheus HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/9 &&
printf "GET /dashboard/prometheus/default HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/10 &&
printf "GET /dashboard HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/11 &&
printf "GET /favicon.ico HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/12 &&
LLVM_PROFILE_FILE="pcm-sensor-server.profraw" bin/tests/pcm-sensor-server-fuzz -max_total_time=$((10 * $factor)) corpus > /dev/null &&
rm -rf corpus/* && cp ../tests/urltest-fuzz.corpus/* corpus/ && LLVM_PROFILE_FILE="urltest.profraw" bin/tests/urltest-fuzz -max_total_time=$((10 * $factor)) corpus > /dev/null &&
rm -rf corpus/* && LLVM_PROFILE_FILE="pcm.profraw" bin/tests/pcm-fuzz -max_total_time=$((5 * $factor)) corpus > /dev/null &&
rm -rf corpus/* && LLVM_PROFILE_FILE="pcm.no_perf.profraw" PCM_NO_PERF=1 bin/tests/pcm-fuzz -max_total_time=$((5 * $factor)) corpus  > /dev/null &&
rm -rf corpus/* && LLVM_PROFILE_FILE="pcm.uncore_perf.profraw" PCM_USE_UNCORE_PERF=1 bin/tests/pcm-fuzz -max_total_time=$((5 * $factor)) corpus > /dev/null &&
rm -rf corpus/* && LLVM_PROFILE_FILE="pcm.nmi_watchdog.profraw" PCM_KEEP_NMI_WATCHDOG=1 bin/tests/pcm-fuzz -max_total_time=$((1 * $factor)) corpus > /dev/null &&
rm -rf corpus/* && LLVM_PROFILE_FILE="pcm-memory.profraw" bin/tests/pcm-memory-fuzz -max_total_time=$((10 * $factor)) corpus > /dev/null &&
llvm-profdata merge -sparse \
        urltest.profraw \
        pcm.profraw \
        pcm.no_perf.profraw \
        pcm.uncore_perf.profraw \
        pcm.nmi_watchdog.profraw \
        pcm-memory.profraw \
        pcm-sensor-server.profraw \
        -o all.profdata &&
llvm-cov report --summary-only \
        -object ./bin/tests/pcm-fuzz \
        -object ./bin/tests/urltest-fuzz \
        -object ./bin/tests/pcm-memory-fuzz \
        -object ./bin/tests/pcm-sensor-server-fuzz \
        -instr-profile=all.profdata | tee report.txt

