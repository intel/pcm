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

./pcm-iio.x -i=1
if [ "$?" -ne "0" ]; then
    echo "Error in pcm-iio.x"
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
# e.g for ./pcm-sensor-server.x, ./pcm-sensor.x, ...

pushd unitTest/
make
./urltest
# We have 2 expected errors, anything else is a bug
if [ "$?" != 2 ]; then
    echo "Error in urltest, 2 expected errors but found $?!"
    exit 1
fi
popd


### Check pcm-raw with event files
# Download nescessary files
if [ ! -f "mapfile.csv" ]; then
    echo "Downloading https://download.01.org/perfmon/mapfile.csv"
    wget -q --timeout=10 https://download.01.org/perfmon/mapfile.csv
    if [ "$?" -ne "0" ]; then
        echo "Could not download mapfile.csv"
        exit 1
    fi
fi

VENDOR=$(lscpu | grep "Vendor ID:" | awk '{print $3}')
FAMILY=$(lscpu | grep "CPU family:" | awk '{print $3}')
MODEL=$(lscpu | grep "Model:" | awk '{printf("%x", $2)}')
STRING="${VENDOR}-${FAMILY}-${MODEL}-"
FILES=$(grep $STRING "mapfile.csv" | awk -F "\"*,\"*" '{print $3}')
DIRS=

for FILE in $FILES
do
    DIR="$(dirname $FILE)"
    DIR="${DIR#?}"
    if [[ ! " ${DIRS[*]} " =~ " ${DIR} " ]]; then
        DIRS+="${DIR} "
    fi
done

for DIR in $DIRS
do
    if [ ! -d $DIR ]; then
        mkdir $DIR
        cd $DIR

        DIRPATH="https://download.01.org/perfmon/${DIR}/"
        echo "Downloading all files from ${DIRPATH}"

        wget -q --timeout=10 -r -l1 --no-parent -A "*.json" $DIRPATH
        if [ "$?" -ne "0" ]; then
            cd ..
            echo "Could not download $DIR"
            exit 1
        fi
        wget -q --timeout=10 -r -l1 --no-parent -A "*.tsv" $DIRPATH
        mv download.01.org/perfmon/${DIR}/* .
        rm -rf download.01.org
        cd ..
    fi
done

# Run workaround to avoid 'Performance Monitoring Unit is occupied by other application'
# errors when running pcm-raw.x
PCM_NO_PERF=1 ./pcm.x -r 2> /dev/null > /dev/null -- sleep 0

# Now check pcm-raw with JSON files from mapFile.csv
./pcm-raw.x -e LD_BLOCKS.STORE_FORWARD -e CPU_CLK_UNHALTED.THREAD_ANY -e INST_RETIRED.ANY  -- sleep 1
if [ "$?" -ne "0" ]; then
    echo "Error in pcm-raw.x"
    exit 1
fi

# Now get corresponding TSV files and replace JSON files in mapFile.csv with them
cp "mapfile.csv" "mapfile.csv_orig"
for FILE in $FILES
do
    DIR="$(dirname $FILE)"
    DIR="${DIR#?}"
    cd $DIR
    BASE="$(basename $FILE)"
    TYPE="$(echo $BASE | sed 's/_v[0-9].*json//g')"
    # TYPE can be for example: skylakex_core or skylakex_uncore.
    CMD="find . -type f -regex '\.\/${TYPE}_v[0-9]*\.[0-9]*.tsv'"
    TSVFILE=$(eval $CMD)
    TSVFILE="${TSVFILE:2}"
    cd ..
    CMD="sed -i 's/${BASE}/${TSVFILE}/g' mapfile.csv"
    eval $CMD
done

# Run workaround to avoid 'Performance Monitoring Unit is occupied by other application'
# errors when running pcm-raw.x
PCM_NO_PERF=1 ./pcm.x -r 2> /dev/null > /dev/null -- sleep 0

# Check pcm-raw with TSV files
./pcm-raw.x -e LD_BLOCKS.STORE_FORWARD -e CPU_CLK_UNHALTED.THREAD_ANY -e INST_RETIRED.ANY  -- sleep 1
if [ "$?" -ne "0" ]; then
    echo "Error in pcm-raw.x"
    rm -rf mapfile.csv
    cp "mapfile.csv_orig" "mapfile.csv"
    exit 1
fi
rm -rf mapfile.csv
cp "mapfile.csv_orig" "mapfile.csv"
