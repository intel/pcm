#!/bin/bash

set -e

usage() {
  echo
  echo "Usage: $0 target_address:port"
  echo
  echo "target_address is the hostname or IP address of the system that runs pcm-sensor-server"
  echo
  echo "Alternative usage: $0 filename"
  echo
  echo "Specify filename containing target_address:port in each line"
  exit 1
}

# Validate the URL format and reject localhost or 127.0.0.1
validate_url() {
  local url=$1
  local regex='^([a-zA-Z0-9.-]+):[0-9]+$'
  local localhost_regex='^(localhost|127\.0\.0\.1):[0-9]+$'

  if ! [[ $url =~ $regex ]]; then
    echo "Error: The target_address ($url) provided is not in the correct format."
    usage
  fi

  if [[ $url =~ $localhost_regex ]]; then
    echo "Error: The target_address cannot be localhost or 127.0.0.1."
    usage
  fi
}

if [ "$#" -ne 1 ]; then
  usage
fi

CTR_RUN=${CTR_RUN:-docker}

mkdir -p grafana_volume/dashboards || { echo "Error creating grafana_volume/dashboards directory"; exit 1; }
mkdir -p prometheus_volume || { echo "Error creating prometheus_volume directory"; exit 1; }

chmod -R 777 *_volume || { echo "Error setting permissions on volume directories"; exit 1; }

mkdir -p provisioning/datasources || { echo "Error creating provisioning/datasources directory"; exit 1; }
cp automatic_prometheus.yml provisioning/datasources/automatic.yml || { echo "Error copying automatic_prometheus.yml"; exit 1; }

# check if argument is file, create the prometheus.yml accordingly
if [ -f "$1" ]; then
  echo "creating prometheus.yml for hosts in targets file";
  head -n -1 "prometheus.yml.template" > prometheus.yml || { echo "Error creating prometheus.yml"; exit 1; }
  while read -r line; do
    validate_url "$line"
    echo "    - targets: ['$line']" >> "prometheus.yml"
  done < "$1"
  echo Downloading PCM dashboard
  curl -o grafana_volume/dashboards/pcm-dashboard.json $(head -1 "$1")/dashboard/prometheus || { echo "Error downloading PCM dashboard"; exit 1; }
else
  validate_url "$1"
  echo "creating prometheus.yml for $1 ";
  sed "s#PCMSENSORSERVER#$1#g" prometheus.yml.template > prometheus.yml || { echo "Error creating prometheus.yml"; exit 1; }
  echo Downloading PCM dashboard
  curl -o grafana_volume/dashboards/pcm-dashboard.json "$1"/dashboard/prometheus || { echo "Error downloading PCM dashboard"; exit 1; }
fi

echo "Starting prometheus network"
${CTR_RUN} network create prometheus-network || { echo "Error creating prometheus network"; exit 1; }
echo Starting prometheus
${CTR_RUN} run --name prometheus --network=prometheus-network -d -p 9090:9090 -v "$PWD"/prometheus.yml:/etc/prometheus/prometheus.yml:Z -v "$PWD"/prometheus_volume:/prometheus:Z quay.io/prometheus/prometheus:latest || { echo "Error starting prometheus"; exit 1; }
echo Starting grafana
${CTR_RUN} run -d --network=prometheus-network --name=grafana -p 3000:3000 -v "$PWD"/grafana_volume:/var/lib/grafana:Z -v "$PWD"/provisioning:/etc/grafana/provisioning:Z -e GF_DASHBOARDS_MIN_REFRESH_INTERVAL=1s docker.io/grafana/grafana:latest || { echo "Error starting grafana"; exit 1; }

echo "Start browser at http://"`hostname`":3000/ or http://localhost:3000/ and login with admin user, password admin"
