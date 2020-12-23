--------------------------------------------------------------------------------
Instructions on How To Run PCM Grafana Dashboard
--------------------------------------------------------------------------------

Installation on target system to be analyzed:
1.  Build: `make pcm-sensor-server.x`
2.  As root start pcm-sensor-server.x: `sudo ./pcm-sensor-server.x`

Alternatively one can start [pcm-sensor-server as a container from docker hub](https://github.com/opcm/pcm/blob/master/DOCKER_README.md).

Installation of the grafana front-end (can be on any *host* system with connectivity to the target system):
1.  Make sure curl and docker are installed on the *host*
2.  In PCM source directory on the *host*: `cd grafana`
3.  (Download once and) start docker containers on the *host*: `sh start.sh http://target_system_address:9738`
       - `start.sh` script starts telegraf/influxdb/grafana containers
       - `start-prometheus.sh` is an alternative script which starts prometheus + grafana containers: `sh start-prometheus.sh target_system_address:9738`
       - Don't use `localhost` to specify the `target_system_address` if the *host* and the target are the same machine because `localhost` resolves to the own private IP address of the docker container when accessed inside the container. The external IP address or hostname should be used instead.
4.  Start your browser at http://*host*:3000/ and then login with admin user, password admin . Change the password and then click on "**Home**" (left top corner) -> "Processor Counter Monitor (PCM) Dashboard"
5.  You can also stop and delete the containers when needed: `sh stop.sh`

![pcm grafana output](https://raw.githubusercontent.com/wiki/opcm/pcm/pcm-dashboard-full.png)
