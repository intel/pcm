--------------------------------------------------------------------------------
Instructions on How-To Run PCM Grafana Dashboard
--------------------------------------------------------------------------------

Installation on target system to be analyzed:
- Build: `make pcm-sensor-server.x`
- As root start pcm-sensor-server-realtime.sh script: `sudo sh pcm-sensor-server-realtime.sh`

Installation of the grafana front-end (can be on any *host* system with connectivity to the target system):
- Make sure curl and docker are installed on the *host*
- In PCM source directory on the *host*: `cd grafana`
- (Download once and) start docker containers on the *host*: `sh start.sh http://target_system_address:80`
- Start your browser at http://*host*:3000/ and then login with admin user, password admin . Change the password and then click on "**Home**" (left top corner) -> "Processor Counter Monitor (PCM) Dashboard"
- You can also stop and delete the containers when needed: `sh stop.sh`
