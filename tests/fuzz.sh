
CC=`which clang` CXX=`which clang++` cmake ..  -DCMAKE_BUILD_TYPE=Debug -DFUZZ=1 && mkdir corpus &&
make urltest-fuzz &&
bin/tests/urltest-fuzz -max_total_time=30 corpus &&
llvm-profdata merge -sparse default.profraw -o default.profdata &&
llvm-cov report --summary-only  ./bin/tests/urltest-fuzz -instr-profile=default.profdata

