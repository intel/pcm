#!/bin/sh

set -e

usage() {
  echo
  echo "Usage: $0 http(s)://target_address:port"
  echo
  echo "target_address is the hostname or IP address of the system that runs pcm-sensor-server"
  exit 1
}

if [ "$#" -ne 1 ]; then
  usage
fi

mkdir -p grafana_volume/dashboards || { echo "Error creating grafana_volume/dashboards directory"; exit 1; }
mkdir -p influxdb_volume || { echo "Error creating influxdb_volume directory"; exit 1; }

chmod -R 777 *_volume || { echo "Error setting permissions on volume directories"; exit 1; }

mkdir -p provisioning/datasources || { echo "Error creating provisioning/datasources directory"; exit 1; }
cp automatic_influxdb.yml provisioning/datasources/automatic.yml || { echo "Error copying automatic_influxdb.yml"; exit 1; }

CTR_RUN=${CTR_RUN:-docker}

# check if argument is file, create the telegraf.conf accordingly
if [ -f "$1" ]; then
  echo "creating telegraf.conf for hosts in targets file";
  head -n -7 "telegraf.conf.template" > telegraf.conf || { echo "Error creating telegraf.conf"; exit 1; }
  while IFS='' read -r line || [[ -n "$line" ]]; do
    # Split the line at the : character to get the IP and port
    ip=$(echo "$line" | cut -d ':' -f 1)
    port=$(echo "$line" | cut -d ':' -f 2)
    # Append the transformed line to the output file, separated by a comma
    echo -n "\"http://$ip:$port/persecond/\"," >> telegraf.conf
  done < "$1"
  sed -i '$ s/,$//' telegraf.conf || { echo "Error editing telegraf.conf"; exit 1; }
  tail -n -6 "telegraf.conf.template" >> telegraf.conf || { echo "Error appending to telegraf.conf"; exit 1; }
  echo Downloading PCM dashboard
  curl -o grafana_volume/dashboards/pcm-dashboard.json $(head -1 "$1")/dashboard || { echo "Error downloading PCM dashboard"; exit 1; }
else
  echo "creating telegraf.conf for $1 ";
  sed "s#PCMSENSORSERVER#$1#g" telegraf.conf.template > telegraf.conf || { echo "Error creating telegraf.conf"; exit 1; }
  echo Downloading PCM dashboard
  curl -o grafana_volume/dashboards/pcm-dashboard.json "$1"/dashboard || { echo "Error downloading PCM dashboard"; exit 1; }
fi

echo "Creating influxdb network"
${CTR_RUN} network create influxdb-network || { echo "Error creating influxdb network"; exit 1; }
echo Starting influxdb
${CTR_RUN} run -d --name influxdb -p 8083:8083 -p 8086:8086 --network=influxdb-network -v "$PWD"/influxdb_volume:/var/lib/influxdb influxdb:1.8.0-alpine || { echo "Error starting influxdb"; exit 1; }
echo Starting telegraf
${CTR_RUN} run -d --name telegraf --network=influxdb-network -v "$PWD"/telegraf.conf:/etc/telegraf/telegraf.conf:ro telegraf || { echo "Error starting telegraf"; exit 1; }
echo Starting grafana
${CTR_RUN} run -d --network=influxdb-network --name grafana -p 3000:3000 -v "$PWD"/provisioning:/etc/grafana/provisioning -v "$PWD"/grafana_volume:/var/lib/grafana grafana/grafana || { echo "Error starting grafana"; exit 1; }

echo "Start browser at http://localhost:3000/ and login with admin user, password admin"
