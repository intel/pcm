--------------------------------------------------------------------------------
Helm chart instructions
--------------------------------------------------------------------------------

### Features:

- Configurable as non-privileged container (value: `privileged`, default: false) and privileged container,
- Support for bare-metal and VM host configurations (files: [values-metal.yaml](values-metal.yaml), [values-vm.yaml](values-vm.yaml)),
- Ability to deploy multiple releases alongside configured differently to handle different kinds of machines (bare-metal, VM) at the [same time](#heterogeneous-mixed-vmmetal-instances-cluster),
- Linux Watchdog handling (controlled with `PCM_KEEP_NMI_WATCHDOG`, `PCM_NO_AWS_WORKAROUND`, `nmiWatchdogMount` values).
- Deploy to own namespace with "helm install ... **-n pcm --create-namespace**".
- Silent mode (value: `silent`, default: false).
- Backward compatible with older Linux kernels (<5.8) - (value: cap_perfmon, default: false).
- VerticalPodAutoscaler (value: `verticalPodAutoscaler.enabled`, default: false)

Here are available methods in this chart of metrics collection w.r.t interfaces and required access:

| Method                  | Used interfaces      | default | Notes                                                                                                   | instructions                                                               |
|-------------------------|----------------------| ------- | ------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------- |
| unprivileged "indirect" | perf, resctrl        |    v    | recommended, missing metrics: energy metrics  (TODO link to issues/PR or node_exporter/rapl_collector)  | `helm install . pcm`                                                       |
| privileged "indirect"   | perf, resctrl        |         | not recommended, unsecure, no advantages over unprivileged), missing metrics: energy metrics            | `helm install . pcm --set privileged=true`                                 |
| privileged "direct"     | msr                  |         | not recommended, unsecure and requires msr module pre loaded on host                                    | `helm install . pcm -f values-direct-privileged.yaml`                      |
| unprivileged "direct"   | msr                  |         | not recommended, requires msr module and access to /dev/cpu  and /dev/mem (non trivial, like using 3rd plugins) | [link for detailed documentation](docs/direct-unprivileged-deployment.md)  |

For more information about direct/indirect collection methods please see [here](#metric-collection-methods-capabilites-vs-requirements)

#### Integration features:

- node-feature-discovery based nodeSelector and nodeAffinity (values: `nfd`, `nfdBaremetalAffinity`, `nfdRDTAffinity`),
- Examples for non-privileged mode using device plugin ("smarter-devices-manager") or using NRI device-injector plugin (TODO) (file: [values-smarter-devices-cpu-mem.yaml](values-smarter-devices-cpu-mem.yaml) ),
- Integration with NRI balloons policy plugin (value: `nriBalloonsPolicyIntegration`),

#### Debugging features:

- Local image registry for development (file: [values-local-image.yaml](values-local-image.yaml) ),
- Deploy Prometheus operator' PodMonitor (value: `podMonitor`)

### Getting started

#### Indirect non-privileged method using Linux abstractions (perf/resctrl) default.

```sh
helm install pcm . 
```

#### Direct privileged method
```
helm install pcm . -f values-direct-privileged.yaml
```

#### All opt-in features: Node-feature-discovery + Prometheus podMonitor + vertical

```
helm install ... --set nfd=true --set podMonitor=true --set verticalPodAutoscaler.enabled=true
```

### Requirements

- Full set of metrics (uncore/UPI, RDT, energy) requires bare-metal or .metal cloud instance.
- /sys/fs/resctrl has to be mounted on host OS (for default indirect deployment method)
- pod is allowed to be run with privileged capabilities (SYS_ADMIN, SYS_RAWIO) on given namespace in other words: Pod Security Standards allow to run on privileged level,

```
    pod-security.kubernetes.io/enforce: privileged
    pod-security.kubernetes.io/enforce-version: latest
    pod-security.kubernetes.io/audit: privileged
    pod-security.kubernetes.io/audit-version: latest
    pod-security.kubernetes.io/warn: privileged
    pod-security.kubernetes.io/warn-version: latest
```

More information here: https://kubernetes.io/docs/tutorials/security/ns-level-pss/ .

### Defaults

- Indirect method uses Linux abstraction to access event counters (Linux Perf, resctrl) and run container in non-privileged mode.
- hostPort 9738 is exposed on host. (TODO: security review, consider TLS, together with Prometheus scrapping !!).
- Prometheus podMonitor is disabled (enabled it with --set podMonitor=true).

### Validation on local kind cluster

#### Requirements

- kubectl/kind/helm/jq binaries available in PATH,
- docker service up and running.
- full set of metrics available only bare-metal instance or Cloud .metal instance.

#### 1) (Optionally) mount resctrl filesystem (for RDT metrics) to unload "msr" kernel module for validation

```
mount -t resctrl resctrl /sys/fs/resctrl
```

For validation to verify that all metrics are available without msr, unload "msr" module from kernel and perf_event_paranoid has default value
```
rmmod msr
echo 2 > /proc/sys/kernel/perf_event_paranoid
cat /proc/sys/kernel/perf_event_paranoid  # expected value 2
```

#### 2) Create kind based Kubernetes cluster

```
kind create cluster
```

**Note** to be able to collect and test RDT metrics through resctrl filesystem, kind cluster have to be created with additional mounts:
```
nodes:
- role: control-plane
  extraMounts:
  - hostPath: /sys/fs/resctrl
    containerPath: /sys/fs/resctrl
```
e.g. create kind cluster with local registry with [this script](https://kind.sigs.k8s.io/docs/user/local-registry/)
and apply the patch to enable resctrl win following way:

```
wget https://kind.sigs.k8s.io/examples/kind-with-registry.sh

sed -i '/apiVersion: kind.x-k8s.io\/v1alpha4/a \
nodes:\
- role: control-plane\
  extraMounts:\
  - hostPath: /sys/fs/resctrl\
    containerPath: /sys/fs/resctrl\
' kind-with-registry.sh
```

Then create cluster using above patched script:
```
bash kind-with-registry.sh
```

Check that resctrl is available inside kind node:
```
docker exec kind-control-plane ls /sys/fs/resctrl/info
# expected output:
# L3_MON
# MB
# ...
```


and optionally local registry is running (to be used with local pcm build images, more detail [below](development-with-local-images-and-testing))
```
docker ps | grep kind-registry
# expected output:
# e57529be23ea   registry:2             "/entrypoint.sh /etc…"   3 weeks ago          Up 3 weeks          127.0.0.1:5001->5000/tcp    kind-registry
```

Export kind kubeconfig as default for further kubectl commands:
```
kind export kubeconfig
kubectl get pods -A
```

#### 3) (Optionally) Deploy Node Feature Discovery (nfd)

```
# I.a. Using Kustomize:
kubectl apply -k https://github.com/kubernetes-sigs/node-feature-discovery/deployment/overlays/default?ref=v0.16.0-devel

# I.b. or with Helm Chart:
helm repo add nfd https://kubernetes-sigs.github.io/node-feature-discovery/charts
helm repo update
helm install nfd/node-feature-discovery --namespace node-feature-discovery --create-namespace --generate-name

# II. Check node "labels" with CPU features are added
kubectl get node kind-control-plane -o yaml | grep feature.node
```

#### 4) (Optionally) Deploy Prometheus operator

```
helm repo add prometheus-community https://prometheus-community.github.io/helm-charts
helm install prometheus prometheus-community/kube-prometheus-stack --set prometheus.prometheusSpec.podMonitorSelectorNilUsesHelmValues=false
kubectl get sts prometheus-prometheus-kube-prometheus-prometheus
```

Note: `podMonitorSelectorNilUsesHelmValues` is disabled (set to false) so Prometheus operator will be able to handle PCM podMonitor deployed without extra `podMonitorLabels` or otherwise pcm need to be deployed like this:
`helm install pcm . --set podMonitor=true --set podMonitorLabels.release=prometheus` (assuming Prometheus operator was deployed as "prometheus")


#### 5) (Optionally) Deploy metric-server and vertical-pod-autoscaler

Note this is irrelevant to pcm-sensor-server functionality, but useful to observer pcm pod CPU/memory usage:

a) metric-server

```
helm repo add metrics-server https://kubernetes-sigs.github.io/metrics-server/
helm repo update
helm upgrade --install --set args={--kubelet-insecure-tls} metrics-server metrics-server/metrics-server --namespace kube-system
```

b) vertical pod autoscaler

```
git clone https://github.com/kubernetes/autoscaler
./autoscaler/vertical-pod-autoscaler/hack/vpa-up.sh
```

#### 6) Deploy PCM helm chart

```
# a) Deploy to current namespace with defaults
helm install pcm . 

# b) Alternatively deploy with NFD and/or with Prometheus enabled
helm install pcm . --set podMonitor=true
helm install pcm . --set nfd=true

# c) Alternatively deploy into own "pcm" namespace 
helm install pcm . --namespace pcm 
```

#### 7) Check metrics are exported

Run proxy in background:
```
kubectl proxy &
```

Access PCM metrics directly:

```sh
kubectl get daemonset pcm
kubectl get pods 
podname=`kubectl get pod -l app.kubernetes.io/component=pcm-sensor-server -ojsonpath='{.items[0].metadata.name}'`

curl -Ls http://127.0.0.1:8001/api/v1/namespaces/default/pods/$podname/proxy/metrics
curl -Ls http://127.0.0.1:8001/api/v1/namespaces/default/pods/$podname/proxy/metrics | grep L3_Cache_Misses                                                         # source: core
curl -Ls http://127.0.0.1:8001/api/v1/namespaces/default/pods/$podname/proxy/metrics | grep DRAM_Writes                                                             # source: uncore
curl -Ls http://127.0.0.1:8001/api/v1/namespaces/default/pods/$podname/proxy/metrics | grep Local_Memory_Bandwidth{socket="1",aggregate="socket",source="core"}     # source: RDT
curl -Ls http://127.0.0.1:8001/api/v1/namespaces/default/pods/$podname/proxy/metrics | grep DRAM_Joules_Consumed                                                    # source: energy
```

... or through Prometheus UI/prom tool (requires prometheus operator to be deployed and helm install with with `--set podMonitor=true`):
```
http://127.0.0.1:8001/api/v1/namespaces/default/services/prometheus-kube-prometheus-prometheus:http-web/proxy/graph
promtool query range --step 1m http://127.0.0.1:8001/api/v1/namespaces/default/services/prometheus-kube-prometheus-prometheus:http-web/proxy 'rate(DRAM_Writes{aggregate="system"}[5m])/1e9'
promtool query instant http://127.0.0.1:8001/api/v1/namespaces/default/services/prometheus-kube-prometheus-prometheus:http-web/proxy 'avg by(__name__) ({job="pcm"})'
```

... or through Grafana with generated dashboard:

```


# 1) Download dashboard
curl -Ls http://127.0.0.1:8001/api/v1/namespaces/default/pods/$podname/proxy/dashboard/prometheus -o pcm-dashboard.json

# change default (too small) interval (from 4s to 2m, following Prometheus best practicies of rate being four times larger than scrapping 30s)
# References: 
# https://grafana.com/blog/2020/09/28/new-in-grafana-7.2-__rate_interval-for-prometheus-rate-queries-that-just-work/
# ($__rate_interval is 4 x scrape interval defined in datasource provisioned by prometheus operator, scrape internval is based on Prometheus object which defaults to 30s)
# - https://github.com/prometheus-community/helm-charts/blob/main/charts/kube-prometheus-stack/values.yaml#L1069
# - https://github.com/prometheus-community/helm-charts/blob/main/charts/kube-prometheus-stack/values.yaml#L3381
sed -i 's/4s/$__rate_interval/g' pcm-dashboard.json

# 2) port forward with kubectl (--address=0.0.0.0)
kubectl port-forward -n default service/prometheus-grafana 8002:80 

# 3) User: admin/prom-operator
# or get password kubectl get secret --namespace default prometheus-grafana -o jsonpath="{.data.admin-password}" | base64 --decode ; echo
http://127.0.0.1:8002

# 4) Go to Dashboards/New/Import  and upload:

pcm-dashboard.json

```

### Deploy alternative options

#### Direct (msr access) as privileged container 
```
helm install pcm . -f values-direct-privileged.yaml
```

#### Homogeneous bare metal instances cluster (full set of metrics)

```
helm install pcm . -f values-metal.yaml
```

#### Homogenizer VM instances cluster (limited set of metrics core)

```
helm install pcm . -f values-vm.yaml
```

#### Heterogeneous (mixed VM/metal instances) cluster 

values-metal.yaml requires node-feature-discovery to be preinstallaed
```
helm install pcm-vm . -f values-vm.yaml
helm install pcm-metal . -f values-metal.yaml
```

#### Direct method as non-privileged container (not recommended)

**Note** PCM requires access to /dev/cpu device in read-write mode (MSR access) but it is no possible currently to mount devices in Kubernetes pods/containers in vanilla Kubernetes for unprivileged containers. Please find more about this limitation https://github.com/kubernetes/kubernetes/issues/5607.

To expose necessary devices to pcm-sensor-server, one can use:

a) Kubernetes device plugin (using Kubernetes [CDI](https://kubernetes.io/docs/concepts/extend-kubernetes/compute-storage-net/device-plugins/) interface),
b) containerd plugin (using [NRI](https://github.com/containerd/nri/) interface),

Examples can be find [here](docs/direct-unprivileged-deployment.md).

#### Development (with local images) and testing

1) Setup kind with registry following this instruction: https://kind.sigs.k8s.io/docs/user/local-registry/
```
wget https://kind.sigs.k8s.io/examples/kind-with-registry.sh
bash kind-with-registry.sh
```

2) Build docker image and upload to local registry 

```
# optionally create buildx based builder
mkdir ~/.docker/cli-plugins
curl -sL https://github.com/docker/buildx/releases/download/v0.14.0/buildx-v0.14.0.linux-amd64 -o ~/.docker/cli-plugins/docker-buildx
chmod +x ~/.docker/cli-plugins/docker-buildx
docker buildx create --driver docker-container --name mydocker --use --bootstrap

# Build production image from **project root directory**:
docker build . -t localhost:5001/pcm-local 
docker push localhost:5001/pcm-local

# Build/push **debug** image with single line 
# Debug Dockerfile contains source code of pcm and some debugging utils (like gdb,strace for further analysis)
# Run from deployment/pcm/ directory:
(cd ../.. ;  docker build . -f Dockerfile.debug -t localhost:5001/pcm-local && docker push localhost:5001/pcm-local)
```

3) When deploying to kind cluster pcm use values to switch to local pcm-local image
```
helm install pcm . -f values-local-image.yaml
```

4) Replace pcm-sensor-server with pcm or sleep to be able to run `gdb` or `strace` for example
```
helm upgrade --install pcm . --set debugPcm=true
helm upgrade --install pcm . --set debugSleep=true
```

**TODO:** consider debug options to be removed before release for security reasons

5) Check logs or interact with container directly:
```
# exec into pcm container
kubectl exec -ti ds/pcm -- bash
# or check logs
kubectl logs ds/pcm
```

### Metric collection methods (capabilities vs requirements)



| Metrics               | Available on Hardware         | Available through interface  | Available through method |
| --------------------- | ----------------------------- | ---------------------------- | ------------------------ |
| core                  | bare-metal, VM (any)          | msr or perf                  | any                      |
| uncore (UPI)          | bare-metal, VM (all sockets)  | msr or perf                  | any                      |
| RDT (MBW,L3OCCUP)     | bare-metal, VM (all sockets)  | msr or resctrl               | any                      |
| energy, temp          | bare-metal (only)             | msr                          | direct                   |
| perf-topdown          |                               | perf only                    | indirect                 |


| Interface     | Requirements                                               |  Controlled by (env/helm value) |  default helm         | Used by source code                                      | Notes                                               |
|---------------|------------------------------------------------------------|---------------------------------|-----------------------|----------------------------------------------------------|-----------------------------------------------------|
| perf          | sys_perf_open() perf_paranoid<=0/privileged/CAP_ADMIN      | PCM_NO_PERF                     | use perf              | programPerfEvent(), PerfVirtualControlRegister()         |                                                     |
| perf-uncore   | sys_perf_open() perf_paranoid<=0/privileged/CAP_ADMIN      | PCM_USE_UNCORE_PERF             | use perf for uncore   | programPerfEvent(), PerfVirtualControlRegister()         |                                                     |
| perf-topdown  | /sys/bus/event_source/devices/cpu/events                   | sysMount                        | yes                   | cpucounters.cpp:perfSupportsTopDown()                    | TODO: conflicts with sys/fs/resctrl                 |
| RDT           | uses "msr" or "resctrl" interface                          | PCM_NO_RDT                      | yes                   | cpucounters.cpp:isRDTDisabled()/QOSMetricAvailable()     |                                                     |
| resctrl       | RW: /sys/fs/resctrl                                        | PCM_USE_RESCTRL                 | yes                   | resctrl.cpp                                              | resctrlMount                                    |
| watchdog      | RO/RW: /proc/sys/kernel/nmi_watchdog                       | PCM_KEEP_NMI_WATCHDOG           | yes (tries to disable)| src/cpucounters.cpp:disableNMIWatchdog()                 |                                                     |
| msr           | RW: /dev/cpu/X/msr + privileged or CAP_ADMIN/CAP_RAWIO     | PCM_NO_MSR                      | msr is disabled       | msr.cpp:MsrHandle()                                      | privileged or some method to access /dev/cpu        |
|               | RW: /dev/mem                                               | ?                               | msr is disabled       | cpucounters.cpp:initUncoreObjects, pci.cpp:PCIHandleM()  | privileged or some method to access /dev/cpu        |
|               | RO/RW: /sys/module/msr/parameters                          | PCM_NO_MSR                      | msr is disabled       | msr.cpp:MsrHandle()                                      | sysMount                                            |
|               | RW: /proc/bus/pci                                          | PCM_USE_UNCORE_PERF             | msr is disabled       | pci.cpp:PCIHandle()                                      | pciMount                                            |
|               | RO: /sys/firmware/acpi/tables/MCFG                         | PCM_USE_UNCORE_PERF             | msr is disabled       | pci.cpp:PciHandle::openMcfgTable()                       | mcfgMount                                           |
|               | energy                                                     |                                 |                       | cpucounters.cpp initEnergyMonitoring()                   |                                                     |


