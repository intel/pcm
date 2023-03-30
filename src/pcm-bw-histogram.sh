#!/bin/sh


if [ "$#" -ne 1 ]; then
  echo
  echo "Usage: $0 <duration>" >&2
  echo
  echo "duration is in the same format as the argument of sleep command:"
  echo
  sleep --help
  exit 1
fi

out=bw-tmp

rm $out

echo
echo ========= CHECKING FOR PMM SUPPORT =========
echo
./pcm-memory -pmm -- sleep 1 >tmp 2>&1
dram_only=`cat tmp | grep "PMM traffic metrics are not available"  | wc -l`
rm tmp
if [ $dram_only -gt 0 ]
then
                echo PMM support is not present
else
                echo PMM support is present
fi


echo
echo ========= MEASURING =========
echo

if [ $dram_only -gt 0 ]
then
                chrt --rr 1 nice --adjustment=-20 ./pcm-memory 0.005 -nc -csv=$out -- sleep $1
else
                chrt --rr 1 nice --adjustment=-20 ./pcm-memory 0.005 -pmm -nc -csv=$out -- sleep $1
fi

cat $out | sed 's/;/,/g' > $out.csv

num_sockets=`lscpu | grep Socket | awk '{print $2}'`

echo
echo ======== POST-PROCESSING ====
echo

for s in `seq 0 $(($num_sockets-1))`; do

echo ============ Socket $s ============;
if [ $dram_only -gt 0 ]
then
                cat $out.csv | cut -d, -f$((4*s+6)) | awk '{ n=n+1; f[int($1/10000)] = f[int($1/10000)] + 1; } END { print "bandwidth(GB/s),count,time(%),chart"; for (i=0; i<32; i++) { if(i in f){ v=100.*f[i]/n; printf "%d-%d\t,%d\t,%3.2f\t,",i*10,(i+1)*10,f[i],v; for (j=0; j<v; j++) printf "#"; print "";  }}}';
else
                cat $out.csv | cut -d, -f$((5*s+7)) | awk '{ n=n+1; f[int($1/10000)] = f[int($1/10000)] + 1; } END { print "Memory bandwidth(GB/s),count,time(%),chart"; for (i=0; i<32; i++) { if(i in f){ v=100.*f[i]/n; printf "%d-%d\t,%d\t,%3.2f\t,",i*10,(i+1)*10,f[i],v; for (j=0; j<v; j++) printf "#"; print "";  }}}';
                cat $out.csv | cut -d, -f$((5*s+5)) | awk '{ n=n+1; f[int($1/1000)] = f[int($1/1000)] + 1; } END { print "PMM read bandwidth(GB/s),count,time(%),chart"; for (i=0; i<32; i++) { if(i in f){ v=100.*f[i]/n; printf "%d-%d\t,%d\t,%3.2f\t,",i,(i+1),f[i],v; for (j=0; j<v; j++) printf "#"; print "";  }}}';
                cat $out.csv | cut -d, -f$((5*s+6)) | awk '{ n=n+1; f[int($1/1000)] = f[int($1/1000)] + 1; } END { print "PMM write bandwidth(GB/s),count,time(%),chart"; for (i=0; i<32; i++) { if(i in f){ v=100.*f[i]/n; printf "%d-%d\t,%d\t,%3.2f\t,",i,(i+1),f[i],v; for (j=0; j<v; j++) printf "#"; print "";  }}}';

fi

done

