# Processor Counter Monitor (PCM) Prometheus exporter [![Build Status](https://travis-ci.com/opcm/pcm.svg?branch=master)](https://travis-ci.com/opcm/pcm) [![pulls](https://img.shields.io/docker/pulls/opcm/pcm.svg)](https://github.com/opcm/pcm/blob/master/DOCKER_README.md)


pcm-sensor-server is a collector exposing Intel processor metrics over http in JSON or Prometheus (exporter text based) format. Also [available as a docker container](https://github.com/opcm/pcm/blob/master/DOCKER_README.md).

Installation on target system to be analyzed:
1.  [Build](https://github.com/opcm/pcm#building-pcm-tools) or [download](https://github.com/opcm/pcm#downloading-pre-compiled-pcm-tools) pcm tools
2.  As root, start pcm-sensor-server.x: `sudo ./pcm-sensor-server.x`

Alternatively one can start [pcm-sensor-server as a container from docker hub](https://github.com/opcm/pcm/blob/master/DOCKER_README.md).

Additional options:

```
$ ./pcm-sensor-server.x --help
Usage: ./pcm-sensor-server.x [OPTION]

Valid Options:
    -d                   : Run in the background
    -p portnumber        : Run on port <portnumber> (default port is 9738)
    -r|--reset           : Reset programming of the performance counters.
    -D|--debug level     : level = 0: no debug info, > 0 increase verbosity.
    -R|--real-time       : If possible the daemon will run with real time
                           priority, could be useful under heavy load to
                           stabilize the async counter fetching.
    -h|--help            : This information
```

The PCM exporter can be used together with Grafana to obtain these Intel processor metrics (see [how-to](https://github.com/opcm/pcm/edit/master/grafana/README.md)):

![pcm grafana output](https://raw.githubusercontent.com/wiki/opcm/pcm/pcm-dashboard-full.png)
