#!/bin/awk -f

{
    for(i=1; i<=NF; i++) {
        if (index($i, term) > 0) print (i)":"$i;
      }
}