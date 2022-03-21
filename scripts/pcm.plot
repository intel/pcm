
set key autotitle columnhead
set datafile separator ","

# change as needed
# set xlabel 'sample # (each is 1000ms)'

set ylabel 'metric value'

set terminal pdf
set output "pcm.pdf"

# change below as needed
# plot metrics 3 .. 37
do for [m=3:37] {
    plot "single_header.pcm.csv" using m with dots
}

# plot metrics 84 .. 107
do for [m=84:107] {
    plot "single_header.pcm.csv" using m with dots
}


