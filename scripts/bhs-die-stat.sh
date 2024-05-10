#!/bin/bash


echo "Intel(r) Performance Counter Monitor"
echo "Birch Stream Die Statistics Utility"
echo

# Run the pcm-tpmi command and store the output
output=$(pcm-tpmi 2 0x10 -d)

# Use a while loop to read each line of the output
echo "$output" | while read -r line; do
    # Check if the line contains "Read value"
    if [[ $line =~ Read\ value\ ([0-9]+)\ from\ TPMI\ ID\ 2@16\ for\ entry\ ([0-9]+)\ in\ instance\ ([0-9]+) ]]; then
        # Extract the value using BASH_REMATCH
        value=${BASH_REMATCH[1]}
	die=${BASH_REMATCH[2]}
	socket=${BASH_REMATCH[3]}

        freq=$(( (value & 0x7F) * 100 ))
        compute=$(( (value >> 23) & 1 ))
        llc=$(( (value >> 24) & 1 ))
        memory=$(( (value >> 25) & 1 ))
        io=$(( (value >> 26) & 1 ))

        die_type=""
        if [ "$compute" -ne 0 ]; then
                die_type="compute/"
        fi
        if [ "$llc" -ne 0 ]; then
                die_type="${die_type}LLC/"
        fi
        if [ "$memory" -ne 0 ]; then
                die_type="${die_type}memory/"
        fi
        if [ "$io" -ne 0 ]; then
                die_type="${die_type}IO"
        fi
        die_type="${die_type%"${die_type##*[!\/]}"}"
        str="Socket $socket die $die ($die_type) uncore frequency"
        printf "%-60s: %d MHz\n" "$str" "$freq"
    fi
done

