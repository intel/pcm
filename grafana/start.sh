#!/bin/sh

if [ "$#" -ne 1 ];
then
  echo
  echo "Usage: $0 http(s)://target_address:port"
  echo
  echo "target_address is the hostname or IP address of the system that runs pcm-sensor-server"
  exit 1
fi

sed "s#PCMSENSORSERVER#$1#g" telegraf.conf.template > telegraf.conf

mkdir -p grafana_volume/dashboards
mkdir -p influxdb_volume

chmod -R 777 *_volume

mkdir -p provisioning/datasources
cp automatic_influxdb.yml provisioning/datasources/automatic.yml

echo Downloading PCM dashboard
curl -o grafana_volume/dashboards/pcm-dashboard.json $1/dashboard

echo Starting influxdb
docker run -d --name influxdb -p 8083:8083 -p 8086:8086 -v $PWD/influxdb_volume:/var/lib/influxdb influxdb
echo Starting telegraf
docker run -d --name telegraf --link=influxdb -v $PWD/telegraf.conf:/etc/telegraf/telegraf.conf:ro telegraf
echo Starting grafana
docker run -d --link=influxdb --name=grafana -p 3000:3000 -v $PWD/provisioning:/etc/grafana/provisioning -v $PWD/grafana_volume:/var/lib/grafana grafana/grafana

echo Start browser at http://localhost:3000/ and login with admin user, password admin

