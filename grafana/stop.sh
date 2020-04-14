
for c in grafana telegraf influxdb; do

	echo Stopping and deleting $c
	docker rm $(docker stop $(docker ps -a -q --filter="name=$c" --format="{{.ID}}"))

done

