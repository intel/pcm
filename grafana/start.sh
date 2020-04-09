echo Starting influxdb
docker run -d --name influxdb -p 8083:8083 -p 8086:8086 influxdb
echo Starting telegraf
docker run -d --name telegraf --link=influxdb -v $PWD/telegraf.conf:/etc/telegraf/telegraf.conf:ro telegraf
echo Starting grafana
docker run -d --link=influxdb --name=grafana -p 3000:3000 -v $PWD/provisioning:/etc/grafana/provisioning grafana/grafana

