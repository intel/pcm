--------------------------------------------------------------------------------
Examples of deploying with direct MSR access as non-privileged container
--------------------------------------------------------------------------------

#### Direct method as non-privileged container (not recommended)

##### a) Device injection using 3rd party device-plugin

TO run PCM with as non privileged pod, we can third party devices plugins e.g.:

- https://github.com/smarter-project/smarter-device-manager
- https://github.com/squat/generic-device-plugin
- https://github.com/everpeace/k8s-host-device-plugin

**Warning** This plugins were NOT audited for security concerns, **use it at your own risk**.

Below is example how to pass /dev/cpu and /dev/mem using smarter-device-manager in kind based Kubernetes test cluster.

```
# Label node to deploy device plugin on that node
kubectl label node kind-control-plane smarter-device-manager=enabled

# Install "smarter-device-manager" device plugin with only /dev/cpu and /dev/mem devices enabled:
git clone https://github.com/smarter-project/smarter-device-manager
helm install smarter-device-plugin --create-namespace --namespace smarter-device-plugin smarter-device-manager/charts/smarter-device-manager --set 'config[0].devicematch=^cpu$' --set 'config[0].nummaxdevices=1' --set 'config[1].devicematch=^mem$' --set 'config[1].nummaxdevices=1'

# Check that cpu and mem devices are available - should return "1"
kubectl get node kind-control-plane -o json | jq .status.capacity

# Install pcm helm chart in unprivileged mode with extraResources for cpu and memory devices.
helm install pcm . -f docs/direct-unprivileged-examples/values-direct-unprivileged.yaml -f docs/direct-unprivileged-examples/values-smarter-devices-cpu-mem.yaml 
```

##### b) Device injection using NRI plugin device-injection 

**TODO**: **Warning** This is work in progress, because it is needed to manually specific all /dev/cpu/XX/msr devices, which is unpractical in production (TO BE MOVED TO EXTERNAL FILE).

```
git clone https://github.com/containerd/nri/
(cd nri/plugins/device-injector/ && go build )
docker cp kind-control-plane:/etc/containerd/config.toml config.toml

cat >>config.toml <<EOF
  [plugins."io.containerd.nri.v1.nri"]
    # Disable NRI support in containerd.
    disable = false
    # Allow connections from externally launched NRI plugins.
    disable_connections = false
    # plugin_config_path is the directory to search for plugin-specific configuration.
    plugin_config_path = "/etc/nri/conf.d"
    # plugin_path is the directory to search for plugins to launch on startup.
    plugin_path = "/opt/nri/plugins"
    # plugin_registration_timeout is the timeout for a plugin to register after connection.
    plugin_registration_timeout = "5s"
    # plugin_requst_timeout is the timeout for a plugin to handle an event/request.
    plugin_request_timeout = "2s"
    # socket_path is the path of the NRI socket to create for plugins to connect to.
    socket_path = "/var/run/nri/nri.sock"
EOF

docker cp config.toml kind-control-plane:/etc/containerd/config.toml 
docker exec kind-control-plane systemctl restart containerd
docker exec kind-control-plane systemd-run -u device-injector /device-injector -idx 10 -verbose
docker exec kind-control-plane systemctl status device-injector

helm install pcm . -f docs/direct-unprivileged-examples/values-direct-unprivileged.yaml -f docs/direct-unprivileged-examples/values-device-injector.yaml 
```
