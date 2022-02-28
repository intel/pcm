

cat "$1" | sed  '0,/^#.*[gG][rR][oO][uU][pP]/d' |  head  -n -1 | dos2unix

