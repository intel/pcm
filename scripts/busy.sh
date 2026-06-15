

# simple script to keep all cores busy for some time

time -p bash -c 'for i in $(seq 1 $(nproc)); do perl -e '\''$c=0; for(0..99999999){$c++;}'\'' & done; wait'


