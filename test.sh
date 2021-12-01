modprobe msr

./pcm.x -r -- sleep 1
if [ "$?" -ne "0" ]; then
   echo "Error in pcm.x"
   exit 1
fi

./pcm-memory.x -- sleep 1
if [ "$?" -ne "0" ]; then
    echo "Error in pcm-memory.x"
    exit 1
fi

./pcm-memory.x -rank=1 -- sleep 1
if [ "$?" -ne "0" ]; then
    echo "Error in pcm-memory.x"
    exit 1
fi

./pcm-memory.x -rank=1 -csv -- sleep 1
if [ "$?" -ne "0" ]; then
    echo "Error in pcm-memory.x"
    exit 1
fi

./pcm-raw.x -e core/config=0x30203,name=LD_BLOCKS.STORE_FORWARD/ -e cha/config=0,name=UNC_CHA_CLOCKTICKS/ -e imc/fixed,name=DRAM_CLOCKS  -- sleep 1
if [ "$?" -ne "0" ]; then
    echo "Error in pcm-raw.x"
    exit 1
fi

./pcm-mmio.x 0x0
if [ "$?" -ne "0" ]; then
    echo "Error in pcm-mmio.x"
    exit 1
fi

./pcm-pcicfg.x 0 0 0 0 0
if [ "$?" -ne "0" ]; then
    echo "Error in pcm-pcicfg.x"
    exit 1
fi

./pcm-numa.x -- sleep 1
if [ "$?" -ne "0" ]; then
    echo "Error in pcm-numa.x"
    exit 1
fi

./pcm-core.x -e cpu/umask=0x01,event=0x0e,name=UOPS_ISSUED.STALL_CYCLES/ -- sleep 1
if [ "$?" -ne "0" ]; then
    echo "Error in pcm-core.x"
    exit 1
fi

./c_example.x
if [ "$?" -ne "0" ]; then
    echo "Error in c_example.x"
    exit 1
fi

./c_example_shlib.x
if [ "$?" -ne "0" ]; then
    echo "Error in c_example_shlib.x"
    exit 1
fi

./pcm-msr.x -a 0x30A
if [ "$?" -ne "0" ]; then
    echo "Error in pcm-msr.x"
    exit 1
fi

./pcm-power.x -- sleep 1
if [ "$?" -ne "0" ]; then
    echo "Error in pcm-power.x"
    exit 1
fi

./pcm-pcie.x -- sleep 1
if [ "$?" -ne "0" ]; then
    echo "Error in pcm-pcie.x"
    exit 1
fi

./pcm-latency.x -i=1
if [ "$?" -ne "0" ]; then
    echo "Error in pcm-latency.x"
    exit 1
fi

./pcm-tsx.x -- sleep 1
if [ "$?" -ne "0" ]; then
    echo "Error in pcm-tsx.x"
    exit 1
fi

# TODO add more tests
# e.g for ./pcm-sensor-server.x, ./pcm-iio.x, ./pcm-sensor.x, ...
