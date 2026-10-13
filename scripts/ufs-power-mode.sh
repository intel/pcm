#!/bin/bash

echo "Intel(r) Performance Counter Monitor"
echo "Birch Stream Power Mode Utility"
echo ""

echo " Options:"
echo " --optimized-power-mode   : set optimized power mode"
echo " --latency-optimized-mode : set latency optimized mode"
echo

# Run the pcm-tpmi command to determine I/O and compute dies
output=$(pcm-tpmi 2 0x10 -d -b 26:26)

# Parse the output to build lists of I/O and compute dies
# Store as "instance:entry" to handle multiple instances per socket
io_dies=()
compute_dies=()
declare -A die_types
while read -r line; do
    if [[ $line == *"entry"* && $line == *"instance"* ]]; then
        entry=$(echo "$line" | grep -oP 'entry \K[0-9]+')
        instance=$(echo "$line" | grep -oP 'instance \K[0-9]+')
        die_key="${instance}:${entry}"
        if [[ $line == *"value 1"* ]]; then
            die_types[$die_key]="IO"
	    io_dies+=("$die_key")
        elif [[ $line == *"value 0"* ]]; then
            die_types[$die_key]="Compute"
	    compute_dies+=("$die_key")
        fi
    fi
done <<< "$output"

if [ "$1" == "--optimized-power-mode" ]; then
	echo "Setting optimized power mode..."

    for die_key in "${io_dies[@]}"; do
        instance="${die_key%:*}"
        entry="${die_key#*:}"
        # EFFICIENCY_LATENCY_CTRL_RATIO (Uncore IO)
        pcm-tpmi 2 0x18 -d -i $instance -e $entry -b 28:22 -w 8 

        #EFFICIENCY_LATENCY_CTRL_LOW_THRESHOLD (Uncore IO)
        pcm-tpmi 2 0x18 -d -i $instance -e $entry -b 38:32 -w 13

        #EFFICIENCY_LATENCY_CTRL_HIGH_THRESHOLD(Uncore IO)
        pcm-tpmi 2 0x18 -d -i $instance -e $entry -b 46:40 -w 120

        #EFFICIENCY_LATENCY_CTRL_HIGH_THRESHOLD_ENABLE (Uncore IO)
        pcm-tpmi 2 0x18 -d -i $instance -e $entry -b 39:39 -w 1
    done

    for die_key in "${compute_dies[@]}"; do
        instance="${die_key%:*}"
        entry="${die_key#*:}"
	 # EFFICIENCY_LATENCY_CTRL_RATIO (Uncore Compute)
	 pcm-tpmi 2 0x18 -d -i $instance -e $entry -b 28:22 -w 12
    done

fi

if [ "$1" == "--latency-optimized-mode" ]; then
    echo "Setting latency optimized mode..."

    for die_key in "${io_dies[@]}"; do
        instance="${die_key%:*}"
        entry="${die_key#*:}"
        # EFFICIENCY_LATENCY_CTRL_RATIO (Uncore IO)
        pcm-tpmi 2 0x18 -d -i $instance -e $entry -b 28:22 -w 0

        #EFFICIENCY_LATENCY_CTRL_LOW_THRESHOLD (Uncore IO)
        pcm-tpmi 2 0x18 -d -i $instance -e $entry -b 38:32 -w 0

        #EFFICIENCY_LATENCY_CTRL_HIGH_THRESHOLD(Uncore IO)
        pcm-tpmi 2 0x18 -d -i $instance -e $entry -b 46:40 -w 0

        #EFFICIENCY_LATENCY_CTRL_HIGH_THRESHOLD_ENABLE (Uncore IO)
        pcm-tpmi 2 0x18 -d -i $instance -e $entry -b 39:39 -w 1
    done

    for die_key in "${compute_dies[@]}"; do
        instance="${die_key%:*}"
        entry="${die_key#*:}"
         # EFFICIENCY_LATENCY_CTRL_RATIO (Uncore Compute)
         pcm-tpmi 2 0x18 -d -i $instance -e $entry -b 28:22 -w 0
    done

fi


echo "Dumping TPMI Power control register states..."
echo ""

# Function to extract and calculate metrics from the value
extract_and_print_metrics() {
    local value=$1
    local socket_id=$2
    local die_key=$3
    local numa_node=$4
    local instance=$5
    local die_type=${die_types[$die_key]}
    
    # Extract instance and entry from die_key
    local inst="${die_key%:*}"
    local entry="${die_key#*:}"

    # Extract bits and calculate metrics
    local min_ratio=$(( (value >> 15) & 0x7F ))
    local max_ratio=$(( (value >> 8) & 0x7F ))
    local eff_latency_ctrl_ratio=$(( (value >> 22) & 0x7F ))
    local eff_latency_ctrl_low_threshold=$(( (value >> 32) & 0x7F ))
    local eff_latency_ctrl_high_threshold=$(( (value >> 40) & 0x7F ))
    local eff_latency_ctrl_high_threshold_enable=$(( (value >> 39) & 0x1 ))

    # Convert to MHz or percentage
    min_ratio=$(( min_ratio * 100 ))
    max_ratio=$(( max_ratio * 100 ))
    eff_latency_ctrl_ratio=$(( eff_latency_ctrl_ratio * 100 ))
    eff_latency_ctrl_low_threshold=$(( (eff_latency_ctrl_low_threshold * 100) / 127 ))
    eff_latency_ctrl_high_threshold=$(( (eff_latency_ctrl_high_threshold * 100) / 127 ))

    # Print metrics
    echo -n "Socket ID: $socket_id"
    if [ -n "$numa_node" ]; then
        echo -n ", NUMA node: $numa_node"
    fi
    echo ", instance: $instance, Die: $entry, Type: $die_type"
    echo "MIN_RATIO: $min_ratio MHz"
    echo "MAX_RATIO: $max_ratio MHz"
    echo "EFFICIENCY_LATENCY_CTRL_RATIO: $eff_latency_ctrl_ratio MHz"
    if [ $die_type == "IO" ] ; then
        echo "EFFICIENCY_LATENCY_CTRL_LOW_THRESHOLD: $eff_latency_ctrl_low_threshold%"
        echo "EFFICIENCY_LATENCY_CTRL_HIGH_THRESHOLD: $eff_latency_ctrl_high_threshold%"
        echo "EFFICIENCY_LATENCY_CTRL_HIGH_THRESHOLD_ENABLE: $eff_latency_ctrl_high_threshold_enable"
    fi
    echo
}

# Iterate over all dies and run pcm-tpmi for each to get the metrics
for die_key in "${!die_types[@]}"; do
    instance="${die_key%:*}"
    entry="${die_key#*:}"
    output=$(pcm-tpmi 2 0x18 -d -i $instance -e $entry)

    # Parse the output and extract metrics for each socket
    while read -r line; do
        if [[ $line == *"Read value"* ]]; then
            value=$(echo "$line" | grep -oP 'value \K[0-9]+')
            # Extract instance ID
            inst=$(echo "$line" | grep -oP 'instance \K[0-9]+')
            # Extract entry ID
            ent=$(echo "$line" | grep -oP 'entry \K[0-9]+')
            # Create die_key from instance and entry
            parsed_die_key="${inst}:${ent}"
            # Extract socket ID if present, otherwise fallback to instance ID
            if [[ $line =~ \(socket\ ([0-9]+)\) ]]; then
                socket_id=${BASH_REMATCH[1]}
            else
                socket_id=$inst
            fi
            # Extract NUMA node ID if present in the output (format: "(NUMA node X)")
            numa_node=""
            if [[ $line =~ \(NUMA\ node\ ([0-9]+)\) ]]; then
                numa_node=${BASH_REMATCH[1]}
            fi
            extract_and_print_metrics "$value" "$socket_id" "$parsed_die_key" "$numa_node" "$inst"
        fi
    done <<< "$output"
done

