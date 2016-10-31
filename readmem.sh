

numactl --cpunodebind=0 --membind=0 ./readmem 10 &
numactl --cpunodebind=1 --membind=1 ./readmem 10 &
numactl --cpunodebind=0 --membind=0 ./readmem 10 &
numactl --cpunodebind=1 --membind=1 ./readmem 10 &
numactl --cpunodebind=0 --membind=0 ./readmem 10 &
numactl --cpunodebind=1 --membind=1 ./readmem 10 &
numactl --cpunodebind=0 --membind=0 ./readmem 10 &
numactl --cpunodebind=1 --membind=1 ./readmem 10 &
numactl --cpunodebind=0 --membind=0 ./readmem 10 &
numactl --cpunodebind=1 --membind=1 ./readmem 10 &
numactl --cpunodebind=0 --membind=0 ./readmem 10 &
numactl --cpunodebind=1 --membind=1 ./readmem 10 &

