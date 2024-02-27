
CTR_RUN=${CTR_RUN:-docker}
for c in grafana telegraf influxdb prometheus; do

	id=`${CTR_RUN} ps -a -q --filter="name=$c" --format="{{.ID}}"`
	if [ ! -z "$id" ]
	then
	   echo Stopping and deleting $c
	   ${CTR_RUN} rm $(${CTR_RUN} stop $id)
	fi
done

${CTR_RUN} network rm prometheus-network influxdb-network

