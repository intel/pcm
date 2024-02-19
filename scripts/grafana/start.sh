#!/bin/sh

if [ "$#" -ne 1 ];
then
  echo
  echo "Usage: $0 http(s)://target_address:port"
  echo
  echo "target_address is the hostname or IP address of the system that runs pcm-sensor-server"
  exit 1
fi


mkdir -p grafana_volume/dashboards
mkdir -p influxdb_volume

chmod -R 777 *_volume

mkdir -p provisioning/datasources
cp automatic_influxdb.yml provisioning/datasources/automatic.yml


CTR_RUN=${CTR_RUN:-docker}

# check if argument is file, create the telegraf.conf accordingly
if [ -f "$1" ]; then
  echo "creating telegraf.conf for hosts in targets file";
  head -n -7 "telegraf.conf.template" > telegraf.conf
  while IFS='' read -r line || [[ -n "$line" ]]; do
    # Split the line at the : character to get the IP and port
    ip=$(echo "$line" | cut -d ':' -f 1)
    port=$(echo "$line" | cut -d ':' -f 2)
    # Append the transformed line to the output file, separated by a comma
    echo -n "\"http://$ip:$port/persecond/\"," >> telegraf.conf
  done < $1
  sed -i '$ s/,$//' telegraf.conf
  tail -n -6 "telegraf.conf.template" >> telegraf.conf
  echo Downloading PCM dashboard
  curl -o grafana_volume/dashboards/pcm-dashboard.json $(head -1 $1)/dashboard

else
  echo "creating telegraf.conf for $1 ";
  sed "s#PCMSENSORSERVER#$1#g" telegraf.conf.template > telegraf.conf
  echo Downloading PCM dashboard
  curl -o grafana_volume/dashboards/pcm-dashboard.json $1/dashboard
fi

echo "Creating influxdb network"
${CTR_RUN} network create influxdb-network
echo Starting influxdb
${CTR_RUN} run -d --name influxdb -p 8083:8083 -p 8086:8086 --network=influxdb-network -v $PWD/influxdb_volume:/var/lib/influxdb influxdb:1.8.0-alpine
echo Starting telegraf
${CTR_RUN} run -d --name telegraf --network=influxdb-network -v $PWD/telegraf.conf:/etc/telegraf/telegraf.conf:ro telegraf
echo Starting grafana
${CTR_RUN} run -d --network=influxdb-network --name grafana -p 3000:3000 -v $PWD/provisioning:/etc/grafana/provisioning -v $PWD/grafana_volume:/var/lib/grafana grafana/grafana

echo Start browser at http://localhost:3000/ and login with admin user, password admin

