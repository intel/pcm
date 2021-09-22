

cat "$1" | sed  '0,/^#.*group/d' |  head  -n -1

