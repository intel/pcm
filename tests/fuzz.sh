
CC=`which clang` CXX=`which clang++` cmake ..  -DCMAKE_BUILD_TYPE=Debug -DFUZZ=1 && mkdir -p corpus &&
make urltest-fuzz pcm-fuzz pcm-memory-fuzz -j &&
LLVM_PROFILE_FILE="urltest.profraw" bin/tests/urltest-fuzz -max_total_time=1000 corpus &&
LLVM_PROFILE_FILE="pcm.profraw" bin/tests/pcm-fuzz -max_total_time=500 corpus &&
LLVM_PROFILE_FILE="pcm.no_perf.profraw" PCM_NO_PERF=1 bin/tests/pcm-fuzz -max_total_time=500 corpus &&
LLVM_PROFILE_FILE="pcm.uncore_perf.profraw" PCM_USE_UNCORE_PERF=1 bin/tests/pcm-fuzz -max_total_time=500 corpus &&
LLVM_PROFILE_FILE="pcm.nmi_watchdog.profraw" PCM_KEEP_NMI_WATCHDOG=1 bin/tests/pcm-fuzz -max_total_time=100 corpus &&
LLVM_PROFILE_FILE="pcm-memory.profraw" bin/tests/pcm-memory-fuzz -max_total_time=1000 corpus &&
llvm-profdata merge -sparse urltest.profraw pcm.profraw pcm.no_perf.profraw pcm.uncore_perf.profraw pcm.nmi_watchdog.profraw pcm-memory.profraw -o all.profdata &&
llvm-cov report --summary-only -object ./bin/tests/pcm-fuzz -object ./bin/tests/urltest-fuzz -object ./bin/tests/pcm-memory-fuzz -instr-profile=all.profdata

