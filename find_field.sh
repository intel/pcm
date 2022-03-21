

head -1 $1 | awk -F ',' -v term="$2" -f find_field.awk


