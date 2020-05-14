--------------------------------------------------------------------------------
How To Run Processor Counter Monitor Server Container from Docker Hub
--------------------------------------------------------------------------------

As root user:
1. ``modprobe msr``
2. ``docker run -d --name pcm --privileged -p 9738:9738 opcm/pcm``

This will start pcm-sensor-server container exposing CPU metrics from the whole system at port 9738 
