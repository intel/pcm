
CC=`which clang` CXX=`which clang++` cmake ..  -DCMAKE_BUILD_TYPE=Debug -DFUZZ=1 && mkdir -p corpus &&
make urltest-fuzz &&
LLVM_PROFILE_FILE="urltest.profraw" bin/tests/urltest-fuzz -max_total_time=30 corpus &&
llvm-profdata merge -sparse urltest.profraw -o all.profdata &&
llvm-cov report --summary-only  ./bin/tests/urltest-fuzz -instr-profile=all.profdata

