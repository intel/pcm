
export PCM_ENFORCE_MBM="1"

factor=1

if [ "$#" -eq 1 ]; then
    factor=$1
fi

# Suppress leaks in libcrypto
# Below is caused by openssl leaks
# similar to https://github.com/spdk/spdk/issues/2947
echo leak:libcrypto.so >> pcm_asan_suppression_file
export LSAN_OPTIONS=suppressions="pcm_asan_suppression_file"

echo "Running fuzz tests with running time multiplier $factor"

CC=`which clang` CXX=`which clang++` cmake ..  -DCMAKE_BUILD_TYPE=Debug -DPCM_FUZZ=ON && mkdir -p corpus &&
make urltest-fuzz \
     pcm-fuzz \
     pcm-memory-fuzz \
     pcm-sensor-server-fuzz \
     pcm-sensor-server-ssl-fuzz \
     -j &&
ldd bin/tests/pcm-fuzz &&
rm -rf corpus/* &&
printf '%b' "GET / HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/1 &&
printf '%b' "GET /metrics HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/2 &&
printf '%b' "GET /persecond HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/3 &&
printf '%b' "GET /persecond HTTP/1.1\r\nHost: localhost\r\nAccept: application/json\r\n\r\n" > corpus/3.1 &&
printf '%b' "GET /persecond HTTP/1.1\r\nHost: localhost\r\nAccept: text/plain; version=0.0.4\r\n\r\n" > corpus/3.2 &&
printf '%b' "GET /persecond/1 HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n"   > corpus/4 &&
printf '%b' "GET /persecond/1 HTTP/1.1\r\nHost: localhost\r\nAccept: application/json\r\n\r\n"   > corpus/4.1 &&
printf '%b' "GET /persecond/1 HTTP/1.1\r\nHost: localhost\r\nAccept: text/plain; version=0.0.4\r\n\r\n"   > corpus/4.2 &&
printf '%b' "GET /persecond/10 HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/5 &&
printf '%b' "GET /persecond/10 HTTP/1.1\r\nHost: localhost\r\nAccept: application/json\r\n\r\n" > corpus/5.1 &&
printf '%b' "GET /persecond/10 HTTP/1.1\r\nHost: localhost\r\nAccept: text/plain; version=0.0.4\r\n\r\n" > corpus/5.2 &&
printf '%b' "GET /persecond/100 HTTP/1.1\r\nHost: localhost\r\nAccept: application/json\r\n\r\n" > corpus/6 &&
printf '%b' "GET /metrics HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/7 &&
printf '%b' "GET /dashboard/influxdb HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/8 &&
printf '%b' "GET /dashboard/prometheus HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/9 &&
printf '%b' "GET /dashboard/prometheus/default HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/10 &&
printf '%b' "GET /dashboard HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/11 &&
printf '%b' "GET /favicon.ico HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/12 &&
LLVM_PROFILE_FILE="pcm-sensor-server-ssl.profraw" bin/tests/pcm-sensor-server-ssl-fuzz -detect_leaks=0 -max_total_time=$((10 * $factor)) -rss_limit_mb=10000 corpus > /dev/null &&
rm -rf corpus/* &&
printf '%b' "GET / HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/1 &&
printf '%b' "GET /metrics HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/2 &&
printf '%b' "GET /persecond HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/3 &&
printf '%b' "GET /persecond HTTP/1.1\r\nHost: localhost\r\nAccept: application/json\r\n\r\n" > corpus/3.1 &&
printf '%b' "GET /persecond HTTP/1.1\r\nHost: localhost\r\nAccept: text/plain; version=0.0.4\r\n\r\n" > corpus/3.2 &&
printf '%b' "GET /persecond/1 HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n"   > corpus/4 &&
printf '%b' "GET /persecond/1 HTTP/1.1\r\nHost: localhost\r\nAccept: application/json\r\n\r\n"   > corpus/4.1 &&
printf '%b' "GET /persecond/1 HTTP/1.1\r\nHost: localhost\r\nAccept: text/plain; version=0.0.4\r\n\r\n"   > corpus/4.2 &&
printf '%b' "GET /persecond/10 HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/5 &&
printf '%b' "GET /persecond/10 HTTP/1.1\r\nHost: localhost\r\nAccept: application/json\r\n\r\n" > corpus/5.1 &&
printf '%b' "GET /persecond/10 HTTP/1.1\r\nHost: localhost\r\nAccept: text/plain; version=0.0.4\r\n\r\n" > corpus/5.2 &&
printf '%b' "GET /persecond/100 HTTP/1.1\r\nHost: localhost\r\nAccept: application/json\r\n\r\n" > corpus/6 &&
printf '%b' "GET /metrics HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/7 &&
printf '%b' "GET /dashboard/influxdb HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/8 &&
printf '%b' "GET /dashboard/prometheus HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/9 &&
printf '%b' "GET /dashboard/prometheus/default HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/10 &&
printf '%b' "GET /dashboard HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/11 &&
printf '%b' "GET /favicon.ico HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n" > corpus/12 &&
LLVM_PROFILE_FILE="pcm-sensor-server.profraw" bin/tests/pcm-sensor-server-fuzz -detect_leaks=0 -rss_limit_mb=10000 -max_total_time=$((10 * $factor)) corpus > /dev/null &&
rm -rf corpus/* &&
printf '%b' "http://otto:test@www.intel.com/~otto/file1.txt" > corpus/1 &&
printf '%b' "file://localhost/c/mnt/cd/file2.txt" > corpus/2 &&
printf '%b' "ftp://otto%40yahoo.com:abcd%3B1234@www.intel.com:30/xyz.php?a=1&t=3" > corpus/3 &&
printf '%b' "gopher://otto@hostname1.intel.com:8080/file3.zyx" > corpus/4 &&
printf '%b' "www.intel.com" > corpus/5 &&
printf '%b' "http://www.blah.org/file.html#firstmark" > corpus/6 &&
printf '%b' "http://www.blah.org/file.html#firstmark%21%23" > corpus/7 &&
printf '%b' "localhost" > corpus/8 &&
printf '%b' "https://www.intel.com" > corpus/9 &&
printf '%b' "://google.com/" > corpus/10 &&
printf '%b' "https://intc.com/request?" > corpus/11 &&
printf '%b' "htt:ps//www.intel.com" > corpus/12 &&
printf '%b' "http://www.intel.com:66666/" > corpus/13 &&
printf '%b' "http:///" > corpus/14 &&
printf '%b' "http://[1234::1234::1234/" > corpus/15 &&
printf '%b' "http://@www.intel.com" > corpus/16 &&
printf '%b' "http://otto@:www.intel.com" > corpus/17 &&
printf '%b' "https://:@www.intel.com" > corpus/18 &&
printf '%b' "https://user:@www.intel.com" > corpus/19 &&
printf '%b' "http:www.intel.com/" > corpus/20 &&
printf '%b' "http://ww\x00\x00\x00rstmark\x0a" > corpus/21 &&
LLVM_PROFILE_FILE="urltest.profraw" bin/tests/urltest-fuzz -max_total_time=$((10 * $factor)) corpus > /dev/null &&
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
        pcm-sensor-server-ssl.profraw \
        -o all.profdata &&
llvm-cov report --summary-only \
        -object ./bin/tests/pcm-fuzz \
        -object ./bin/tests/urltest-fuzz \
        -object ./bin/tests/pcm-memory-fuzz \
        -object ./bin/tests/pcm-sensor-server-fuzz \
        -object ./bin/tests/pcm-sensor-server-ssl-fuzz \
        -instr-profile=all.profdata | tee report.txt

