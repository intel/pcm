#!/bin/bash

echo "Intel(r) Performance Counter Monitor"
echo "Birch Stream Power Mode Utility"
echo ""

echo " Options:"
echo " --default                : set default power mode"
echo " --latency-optimized-mode : set latency optimized mode"
echo

# Run the pcm-tpmi command to determine I/O and compute dies
output=$(pcm-tpmi 2 0x10 -d -b 26:26)

# Parse the output to build lists of I/O and compute dies
io_dies=()
compute_dies=()
declare -A die_types
while read -r line; do
    if [[ $line == *"instance 0"* ]]; then
        die=$(echo "$line" | grep -oP 'entry \K[0-9]+')
        if [[ $line == *"value 1"* ]]; then
            die_types[$die]="IO"
	    io_dies+=("$die")
        elif [[ $line == *"value 0"* ]]; then
            die_types[$die]="Compute"
	    compute_dies+=("$die")
        fi
    fi
done <<< "$output"

if [ "$1" == "--default" ]; then
	echo "Setting default mode..."

    for die in "${io_dies[@]}"; do
        # EFFICIENCY_LATENCY_CTRL_RATIO (Uncore IO)
        pcm-tpmi 2 0x18 -d -e $die -b 28:22 -w 8 

        #EFFICIENCY_LATENCY_CTRL_LOW_THRESHOLD (Uncore IO)
        pcm-tpmi 2 0x18 -d -e $die -b 38:32 -w 13

        #EFFICIENCY_LATENCY_CTRL_HIGH_THRESHOLD(Uncore IO)
        pcm-tpmi 2 0x18 -d -e $die -b 46:40 -w 120

        #EFFICIENCY_LATENCY_CTRL_HIGH_THRESHOLD_ENABLE (Uncore IO)
        pcm-tpmi 2 0x18 -d -e $die -b 39:39 -w 1
    done

    for die in "${compute_dies[@]}"; do
	 # EFFICIENCY_LATENCY_CTRL_RATIO (Uncore Compute)
	 pcm-tpmi 2 0x18 -d -e $die -b 28:22 -w 12
    done

fi

if [ "$1" == "--latency-optimized-mode" ]; then
    echo "Setting latency optimized mode..."

    for die in "${io_dies[@]}"; do
        # EFFICIENCY_LATENCY_CTRL_RATIO (Uncore IO)
        pcm-tpmi 2 0x18 -d -e $die -b 28:22 -w 0

        #EFFICIENCY_LATENCY_CTRL_LOW_THRESHOLD (Uncore IO)
        pcm-tpmi 2 0x18 -d -e $die -b 38:32 -w 0

        #EFFICIENCY_LATENCY_CTRL_HIGH_THRESHOLD(Uncore IO)
        pcm-tpmi 2 0x18 -d -e $die -b 46:40 -w 0

        #EFFICIENCY_LATENCY_CTRL_HIGH_THRESHOLD_ENABLE (Uncore IO)
        pcm-tpmi 2 0x18 -d -e $die -b 39:39 -w 1
    done

    for die in "${compute_dies[@]}"; do
         # EFFICIENCY_LATENCY_CTRL_RATIO (Uncore Compute)
         pcm-tpmi 2 0x18 -d -e $die -b 28:22 -w 0
    done

fi


echo "Dumping TPMI Power control register states..."
echo ""

# Function to extract and calculate metrics from the value
extract_and_print_metrics() {
    local value=$1
    local socket_id=$2
    local die=$3
    local die_type=${die_types[$die]}

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
    echo "Socket ID: $socket_id, Die: $die, Type: $die_type"
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
for die in "${!die_types[@]}"; do
    output=$(pcm-tpmi 2 0x18 -d -e "$die")

    # Parse the output and extract metrics for each socket
    while read -r line; do
        if [[ $line == *"Read value"* ]]; then
            value=$(echo "$line" | grep -oP 'value \K[0-9]+')
            socket_id=$(echo "$line" | grep -oP 'instance \K[0-9]+')
            extract_and_print_metrics "$value" "$socket_id" "$die"
        fi
    done <<< "$output"
done

