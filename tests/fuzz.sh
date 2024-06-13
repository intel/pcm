
CC=`which clang` CXX=`which clang++` cmake ..  -DCMAKE_BUILD_TYPE=Debug -DFUZZ=1 && mkdir -p corpus &&
make urltest-fuzz pcm-fuzz -j &&
LLVM_PROFILE_FILE="urltest.profraw" bin/tests/urltest-fuzz -max_total_time=30 corpus &&
LLVM_PROFILE_FILE="pcm.profraw" bin/tests/pcm-fuzz -max_total_time=30 corpus &&
llvm-profdata merge -sparse urltest.profraw pcm.profraw -o all.profdata &&
llvm-cov report --summary-only -object ./bin/tests/pcm-fuzz -object ./bin/tests/urltest-fuzz -instr-profile=all.profdata

