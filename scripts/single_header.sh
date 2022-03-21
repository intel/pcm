

cat $1 | awk -F ',' -f single_header.awk > single_header.$1

