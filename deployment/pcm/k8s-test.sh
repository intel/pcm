####
modprobe msr
lsmod | egrep '^msr'

# Create cluster
kind create cluster
kind export kubeconfig

# Deploy NodeFeatureDiscovery
#kubectl apply -k https://github.com/kubernetes-sigs/node-feature-discovery/deployment/overlays/default?ref=v0.15.1
kubectl apply -k https://github.com/kubernetes-sigs/node-feature-discovery/deployment/overlays/default?ref=v0.16.0-devel
kubectl get node -o jsonpath='{.items[0].metadata.labels.feature\.node\.kubernetes\.io\/cpu\-model\.vendor_id}{"\n"}'
kubectl get nodefeature kind-control-plane -n node-feature-discovery -o yaml
kubectl get node kind-control-plane -o yaml

helm repo add nfd https://kubernetes-sigs.github.io/node-feature-discovery/charts
helm repo update
helm install nfd/node-feature-discovery --namespace node-feature-discovery --create-namespace --generate-name

# Deploy prometheus for PodMonitor
helm repo add prometheus-community https://prometheus-community.github.io/helm-charts
helm install prometheus prometheus-community/kube-prometheus-stack --set prometheus.prometheusSpec.podMonitorSelectorNilUsesHelmValues=false
kubectl get sts prometheus-prometheus-kube-prometheus-prometheus

# Deploy PCM
kubectl apply -f pcm-kubernetes.yaml

# Verfiy PCM works as expected
kubectl -n intel-pcm get daemonset
kubectl -n intel-pcm get pods
podname=`kubectl -n intel-pcm get pods -ojsonpath='{.items[0].metadata.name}'`
kubectl proxy &
curl -Ls http://127.0.0.1:8001/api/v1/namespaces/intel-pcm/pods/$podname/proxy/metrics | grep DRAM_Writes
promtool query instant http://127.0.0.1:8001/api/v1/namespaces/default/services/prometheus-kube-prometheus-prometheus:http-web/proxy 'avg by(__name__) ({job="pcm"})'

# Metrics
```
CStateResidency => 0.09090909090909094 @[1707901856.957]
Clock_Unhalted_Ref => 1010026077.3913049 @[1707901856.957]
Clock_Unhalted_Thread => 1295730425.8695648 @[1707901856.957]
DRAM_Joules_Consumed => 0 @[1707901856.957]
DRAM_Reads => 3600814506.6666665 @[1707901856.957]
DRAM_Writes => 1974366592 @[1707901856.957]
Embedded_DRAM_Reads => 0 @[1707901856.957]
Embedded_DRAM_Writes => 0 @[1707901856.957]
Incoming_Data_Traffic_On_Link_0 => 689786624 @[1707901856.957]
Incoming_Data_Traffic_On_Link_1 => 689454432 @[1707901856.957]
Incoming_Data_Traffic_On_Link_2 => 0 @[1707901856.957]
Instructions_Retired_Any => 749013885.5739133 @[1707901856.957]
Invariant_TSC => 432975372048881700 @[1707901856.957]
L2_Cache_Hits => 3531524.973913045 @[1707901856.957]
L2_Cache_Misses => 2334387.130434784 @[1707901856.957]
L3_Cache_Hits => 1325323.1739130428 @[1707901856.957]
L3_Cache_Misses => 627863.4000000003 @[1707901856.957]
L3_Cache_Occupancy => 0 @[1707901856.957]
Local_Memory_Bandwidth => 0 @[1707901856.957]
Measurement_Interval_in_us => 14507400443881 @[1707901856.957]
Memory_Controller_IO_Requests => 0 @[1707901856.957]
Number_of_sockets => 2 @[1707901856.957]
OS_ID => 55.499999999999986 @[1707901856.957]
Outgoing_Data_And_Non_Data_Traffic_On_Link_0 => 1843333122.5 @[1707901856.957]
Outgoing_Data_And_Non_Data_Traffic_On_Link_1 => 1849219231.5 @[1707901856.957]
Outgoing_Data_And_Non_Data_Traffic_On_Link_2 => 0 @[1707901856.957]
Package_Joules_Consumed => 0 @[1707901856.957]
Persistent_Memory_Reads => 0 @[1707901856.957]
Persistent_Memory_Writes => 0 @[1707901856.957]
RawCStateResidency => 89486131.66409859 @[1707901856.957]
Remote_Memory_Bandwidth => 0 @[1707901856.957]
SMI_Count => 0 @[1707901856.957]
Thermal_Headroom => -2147483648 @[1707901856.957]
Utilization_Incoming_Data_Traffic_On_Link_0 => 0 @[1707901856.957]
Utilization_Incoming_Data_Traffic_On_Link_1 => 0 @[1707901856.957]
Utilization_Incoming_Data_Traffic_On_Link_2 => 0 @[1707901856.957]
Utilization_Outgoing_Data_And_Non_Data_Traffic_On_Link_0 => 0 @[1707901856.957]
Utilization_Outgoing_Data_And_Non_Data_Traffic_On_Link_1 => 0 @[1707901856.957]
Utilization_Outgoing_Data_And_Non_Data_Traffic_On_Link_2 => 0 @[1707901856.957]
```
