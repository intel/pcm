

filename=`curl https://download.opensuse.org/repositories/home:/opcm/SLE_15_SP1/x86_64/ -s | sed -n 's/.*\(pcm-0-[0-9]*\.1\.x86_64.rpm\).*/\1/p'`

curl -L https://download.opensuse.org/repositories/home:/opcm/SLE_15_SP1/x86_64/$filename -o $filename

rpm2cpio $filename | cpio -idmv

