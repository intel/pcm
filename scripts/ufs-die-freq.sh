#!/bin/bash

echo "Intel(r) Performance Counter Monitor"
echo "Uncore Frequency Scaling: Die Frequency Control Utility"
echo

print_usage() {
    echo " Usage: $(basename "$0") --die-type <compute|LLC|memory|IO> [--min <MHz>] [--max <MHz>]"
    echo "                         [--socket <N>] [--numa-node <N>] [--instance <N>] [--die <N>]"
    echo
    echo "   --die-type <type> : die type to configure: compute, LLC, memory or IO"
    echo "   --min <MHz>       : set the minimum uncore frequency (100 MHz granularity, 0..12700)"
    echo "   --max <MHz>       : set the maximum uncore frequency (100 MHz granularity, 0..12700)"
    echo "   --socket <N>      : restrict the operation to socket N (default: all sockets)"
    echo "                       (examples: --socket 1 --socket 0,1 --socket 0,2-3)"
    echo "   --numa-node <N>   : restrict the operation to NUMA node N (default: all NUMA nodes)"
    echo "                       (examples: --numa-node 1 --numa-node 0,1 --numa-node 0,2-3)"
    echo "   --instance <N>    : restrict the operation to TPMI instance N (default: all instances)"
    echo "   --die <N>         : restrict the operation to die (TPMI entry) N (default: all dies)"
    echo
    echo " Without --min/--max the current frequency limits of the matching dies are printed only."
    echo " Note: a die can have several types (e.g. compute/LLC/memory) and is selected by any of them."
    echo
    echo " Examples:"
    echo "   $(basename "$0") --die-type compute --min 2000               # pin compute dies to at least 2 GHz"
    echo "   $(basename "$0") --die-type IO --min 800 --max 2400          # set the IO die frequency window"
    echo "   $(basename "$0") --die-type memory                           # show the memory die frequency limits"
    echo "   $(basename "$0") --die-type compute --max 2400 --socket 0    # limit the compute dies of socket 0"
    echo "   $(basename "$0") --die-type LLC --min 1600 --numa-node 2     # tune the LLC dies of NUMA node 2"
    echo
}

# TPMI UFS (Uncore Frequency Scaling) register offsets and bit fields
UFS_STATUS=0x10        # bits 6:0 current ratio, bits 26:23 die type
UFS_CONTROL=0x18       # bits 14:8 MAX_RATIO, bits 21:15 MIN_RATIO
MAX_RATIO_BITS="14:8"
MIN_RATIO_BITS="21:15"
MIN_MAX_RATIO_BITS="21:8"  # both fields, to update them in a single write

die_type=""
min_freq=""
max_freq=""
socket_filter=""
numa_filter=""
instance_filter=""
die_filter=""

while [ $# -gt 0 ]; do
    # accept both "--option value" and "--option=value"
    opt="${1%%=*}"
    if [[ $1 == *=* ]]; then
        val="${1#*=}"
        shift
    else
        val="$2"
        shift
        [ $# -gt 0 ] && shift
    fi
    case "$opt" in
        -h|--help)  print_usage; exit 0 ;;
        --die-type|--min|--max|--socket|--numa-node|--instance|--die)
            if [ -z "$val" ] || [[ $val == -* ]]; then
                echo "Error: option '$opt' requires a value"
                echo
                print_usage
                exit 1
            fi
            case "$opt" in
                --die-type) die_type="$val" ;;
                --min)      min_freq="$val" ;;
                --max)      max_freq="$val" ;;
                --socket)   socket_filter="$val" ;;
                --numa-node) numa_filter="$val" ;;
                --instance) instance_filter="$val" ;;
                --die)      die_filter="$val" ;;
            esac
            ;;
        *)
            echo "Error: unknown option '$opt'"
            echo
            print_usage
            exit 1
            ;;
    esac
done

if ! command -v pcm-tpmi > /dev/null 2>&1; then
    echo "Error: pcm-tpmi utility is not found in PATH."
    echo "       Please build PCM or install the PCM package (see doc/LATENCY-OPTIMIZED-MODE.md)."
    exit 1
fi

# select the die type bit in the UFS_STATUS register
case "$(echo "$die_type" | tr '[:upper:]' '[:lower:]')" in
    compute|core|cor) die_type_bit=23; die_type_name="compute" ;;
    llc|cache)        die_type_bit=24; die_type_name="LLC" ;;
    memory|mem)       die_type_bit=25; die_type_name="memory" ;;
    io)               die_type_bit=26; die_type_name="IO" ;;
    "")
        echo "Error: --die-type is required."
        echo
        print_usage
        exit 1
        ;;
    *)
        echo "Error: unknown die type '$die_type'."
        echo
        print_usage
        exit 1
        ;;
esac

# Convert a frequency in MHz into an uncore ratio (100 MHz units)
freq_to_ratio() {
    local mhz=$1
    local name=$2
    if ! [[ $mhz =~ ^[0-9]+$ ]]; then
        echo "Error: invalid $name frequency '$mhz' (expected a number in MHz)" >&2
        return 1
    fi
    local ratio=$(( (mhz + 50) / 100 ))
    if [ "$ratio" -gt 127 ]; then
        echo "Error: $name frequency $mhz MHz is out of range (max 12700 MHz)" >&2
        return 1
    fi
    if [ $(( ratio * 100 )) -ne "$mhz" ]; then
        echo "Warning: $name frequency $mhz MHz is rounded to $(( ratio * 100 )) MHz (100 MHz granularity)" >&2
    fi
    echo "$ratio"
}

min_ratio=""
max_ratio=""
if [ -n "$min_freq" ]; then
    min_ratio=$(freq_to_ratio "$min_freq" "minimum") || exit 1
fi
if [ -n "$max_freq" ]; then
    max_ratio=$(freq_to_ratio "$max_freq" "maximum") || exit 1
fi
if [ -n "$min_ratio" ] && [ -n "$max_ratio" ] && [ "$min_ratio" -gt "$max_ratio" ]; then
    echo "Error: minimum frequency $(( min_ratio * 100 )) MHz is higher than maximum frequency $(( max_ratio * 100 )) MHz"
    exit 1
fi

# Expand an integer list like "0,2-3" into "0 2 3"
expand_integer_list() {
    local list=$1
    local name=$2
    local item start end i
    local result=""
    for item in ${list//,/ }; do
        if [[ $item =~ ^([0-9]+)-([0-9]+)$ ]]; then
            start=${BASH_REMATCH[1]}
            end=${BASH_REMATCH[2]}
            if [ "$start" -gt "$end" ]; then
                echo "Error: invalid $name range '$item'" >&2
                return 1
            fi
            for (( i = start; i <= end; ++i )); do
                result="$result $i"
            done
        elif [[ $item =~ ^[0-9]+$ ]]; then
            result="$result $item"
        else
            echo "Error: invalid $name '$item' (expected a list like 1, 0,1 or 0,2-3)" >&2
            return 1
        fi
    done
    echo "$result"
}

sockets=""
numa_nodes=""
location_suffix=""
if [ -n "$socket_filter" ]; then
    sockets=$(expand_integer_list "$socket_filter" "socket") || exit 1
    location_suffix=" on socket(s)${sockets}"
fi
if [ -n "$numa_filter" ]; then
    numa_nodes=$(expand_integer_list "$numa_filter" "NUMA node") || exit 1
    location_suffix="$location_suffix on NUMA node(s)${numa_nodes}"
fi

# Check if the value is in the list (an empty list selects everything)
in_list() {
    local list=$1
    local value=$2
    local i
    [ -z "$list" ] && return 0
    [ -z "$value" ] && return 1
    for i in $list; do
        if [ "$i" -eq "$value" ]; then
            return 0
        fi
    done
    return 1
}

# Check if the die is selected by --socket and --numa-node
die_selected() {
    local socket=$1
    local numa_node=$2
    in_list "$sockets" "$socket" && in_list "$numa_nodes" "$numa_node"
}

# pcm-tpmi instance/entry selection options
select_opts=()
if [ -n "$instance_filter" ]; then
    select_opts+=(-i "$instance_filter")
fi
if [ -n "$die_filter" ]; then
    select_opts+=(-e "$die_filter")
fi

# Read a single TPMI register of one die and print its value
read_die_register() {
    local instance=$1
    local entry=$2
    local offset=$3
    local out
    out=$(pcm-tpmi 2 "$offset" -d -i "$instance" -e "$entry")
    while read -r line; do
        if [[ $line =~ Read\ value\ ([0-9]+) ]]; then
            echo "${BASH_REMATCH[1]}"
            return 0
        fi
    done <<< "$out"
    return 1
}

# Build the die type string out of the UFS_STATUS register value
die_type_string() {
    local value=$1
    local str=""
    if [ $(( (value >> 23) & 1 )) -ne 0 ]; then
        str="compute/"
    fi
    if [ $(( (value >> 24) & 1 )) -ne 0 ]; then
        str="${str}LLC/"
    fi
    if [ $(( (value >> 25) & 1 )) -ne 0 ]; then
        str="${str}memory/"
    fi
    if [ $(( (value >> 26) & 1 )) -ne 0 ]; then
        str="${str}IO/"
    fi
    echo "${str%/}"
}

# Discover the dies and their types
declare -A die_types
declare -A die_locations
matching_dies=()

output=$(pcm-tpmi 2 $UFS_STATUS -d "${select_opts[@]}")

while read -r line; do
    if [[ $line =~ Read\ value\ ([0-9]+)\ from\ TPMI\ ID\ [0-9]+@[0-9]+\ for\ entry\ ([0-9]+)\ in\ instance\ ([0-9]+) ]]; then
        value=${BASH_REMATCH[1]}
        die=${BASH_REMATCH[2]}
        instance=${BASH_REMATCH[3]}

        # Extract socket ID if present in the output (format: "(socket X)")
        if [[ $line =~ \(socket\ ([0-9]+)\) ]]; then
            socket=${BASH_REMATCH[1]}
        else
            # Fallback to instance ID if socket info is not available
            socket=$instance
        fi

        # Extract NUMA node ID if present in the output (format: "(NUMA node X)")
        numa_node=""
        location="Socket $socket"
        if [[ $line =~ \(NUMA\ node\ ([0-9]+)\) ]]; then
            numa_node=${BASH_REMATCH[1]}
            location="$location NUMA node $numa_node"
        fi
        location="$location instance $instance die $die"

        die_key="${instance}:${die}"
        die_types[$die_key]=$(die_type_string "$value")
        die_locations[$die_key]="$location"

        if [ $(( (value >> die_type_bit) & 1 )) -ne 0 ] && die_selected "$socket" "$numa_node"; then
            matching_dies+=("$die_key")
        fi
    fi
done <<< "$output"

if [ ${#die_types[@]} -eq 0 ]; then
    echo "Error: no uncore frequency scaling dies found."
    echo "       The platform may not support TPMI UFS or the utility has no access to MSR/PCICFG drivers"
    echo "       (root privileges are required)."
    exit 1
fi

if [ ${#matching_dies[@]} -eq 0 ]; then
    echo "Error: no $die_type_name dies found$location_suffix. Available dies:"
    for die_key in $(printf '%s\n' "${!die_types[@]}" | sort -t: -k1,1n -k2,2n); do
        echo "       ${die_locations[$die_key]} (${die_types[$die_key]})"
    done
    exit 1
fi

# Apply the requested frequency limits
if [ -n "$min_ratio" ] || [ -n "$max_ratio" ]; then
    echo -n "Setting"
    if [ -n "$min_ratio" ]; then
        echo -n " minimum $(( min_ratio * 100 )) MHz"
    fi
    if [ -n "$max_ratio" ]; then
        echo -n " maximum $(( max_ratio * 100 )) MHz"
    fi
    echo " uncore frequency of the $die_type_name dies$location_suffix..."
    echo

    for die_key in "${matching_dies[@]}"; do
        instance="${die_key%:*}"
        entry="${die_key#*:}"

        control=$(read_die_register "$instance" "$entry" $UFS_CONTROL)
        if [ -z "$control" ]; then
            echo "Warning: can not read the control register of ${die_locations[$die_key]}, skipping it"
            continue
        fi
        current_min=$(( (control >> 15) & 0x7F ))
        current_max=$(( (control >> 8) & 0x7F ))
        new_min=${min_ratio:-$current_min}
        new_max=${max_ratio:-$current_max}

        if [ "$new_min" -gt "$new_max" ]; then
            echo "Warning: ${die_locations[$die_key]}: requested minimum $(( new_min * 100 )) MHz is higher than" \
                 "the maximum $(( new_max * 100 )) MHz, skipping it"
            continue
        fi

        if [ -n "$min_ratio" ] && [ -n "$max_ratio" ]; then
            # MIN_RATIO and MAX_RATIO are adjacent fields: update both in one write
            pcm-tpmi 2 $UFS_CONTROL -d -i "$instance" -e "$entry" -b $MIN_MAX_RATIO_BITS \
                -w $(( (new_min << 7) | new_max )) > /dev/null
        elif [ -n "$min_ratio" ]; then
            pcm-tpmi 2 $UFS_CONTROL -d -i "$instance" -e "$entry" -b $MIN_RATIO_BITS -w "$new_min" > /dev/null
        else
            pcm-tpmi 2 $UFS_CONTROL -d -i "$instance" -e "$entry" -b $MAX_RATIO_BITS -w "$new_max" > /dev/null
        fi
    done
fi

# Print the resulting state of the selected dies
echo "$die_type_name die uncore frequency limits$location_suffix:"
echo

for die_key in "${matching_dies[@]}"; do
    instance="${die_key%:*}"
    entry="${die_key#*:}"

    control=$(read_die_register "$instance" "$entry" $UFS_CONTROL)
    status=$(read_die_register "$instance" "$entry" $UFS_STATUS)
    if [ -z "$control" ] || [ -z "$status" ]; then
        echo "Warning: can not read the registers of ${die_locations[$die_key]}"
        continue
    fi

    min=$(( ((control >> 15) & 0x7F) * 100 ))
    max=$(( ((control >> 8) & 0x7F) * 100 ))
    current=$(( (status & 0x7F) * 100 ))

    printf "%-60s: min %5d MHz, max %5d MHz, current %5d MHz\n" \
        "${die_locations[$die_key]} (${die_types[$die_key]})" "$min" "$max" "$current"
done
