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

./pcm-raw.x -e core/config=0x30203,name=LD_BLOCKS.STORE_FORWARD/ -e cha/config=0,name=UNC_CHA_CLOCKTICKS/ -e imc/fixed,name=DRAM_CLOCKS  -- sleep 1
if [ "$?" -ne "0" ]; then
    echo "Error in pcm-raw.x"
    exit 1
fi

# TODO add more tests


