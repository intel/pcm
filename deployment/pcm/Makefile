
chart-lint-report.txt: values.yaml templates
	docker run -ti --rm -w /pcm -v `realpath $(PWD)/../..`:/pcm quay.io/helmpack/chart-testing ct lint --charts deployment/pcm --validate-maintainers=false | tee chart-lint-report.txt

#
# kind cluster targets
#
# https://stackoverflow.com/questions/649246/is-it-possible-to-create-a-multi-line-string-variable-in-a-makefile
define KIND_EXTRA_MOUNTS
nodes:
- role: control-plane
  extraMounts:
  - hostPath: /sys/fs/resctrl
    containerPath: /sys/fs/resctrl
endef

export KIND_EXTRA_MOUNTS
_kind_with_registry.sh:
	curl -sl https://kind.sigs.k8s.io/examples/kind-with-registry.sh -o _kind_with_registry.sh.tmp
	echo "$$KIND_EXTRA_MOUNTS" >_kind_extra_mounts.txt
	sed '/apiVersion: kind.x-k8s.io\/v1alpha4/r _kind_extra_mounts.txt' _kind_with_registry.sh.tmp >_kind_with_registry.sh
	chmod +x _kind_with_registry.sh

_kind_deploy_cluster: _kind_with_registry.sh
	./_kind_with_registry.sh
	kind export kubeconfig
	touch _kind_deploy_cluster


#
# 1) e2e-default: minimal E2e pcm pod only test
#
kind_deploy_pcm:
	helm install pcm .
	kubectl wait daemonset pcm --for=jsonpath='{.status.numberReady}'=1

kind_pcm_test:
	helm test pcm

e2e-default: _kind_deploy_cluster kind_deploy_pcm kind_pcm_test

#
# 2) e2e-default-local-image: minimal E2e pcm with local image build
#
build_local_image:
	(cd ../.. ; docker build . -t localhost:5001/pcm-local)
	docker push localhost:5001/pcm-local

kind_deploy_pcm_local_image:
	helm upgrade --install --reset-values --wait pcm . -f values-local-image.yaml
	kubectl wait daemonset pcm --for=jsonpath='{.spec.template.spec.containers[0].image'}=localhost:5001/pcm-local:latest
	kubectl wait daemonset pcm --for=jsonpath='{.status.numberReady}'=1

e2e-default-local-image: _kind_deploy_cluster build_local_image kind_deploy_pcm_local_image kind_pcm_test

#
# 3) e2e-prometheus: E2E test for podMonitor (pod monitor test)
#
_kind_deploy_prometheus:
	helm repo add prometheus-community https://prometheus-community.github.io/helm-charts
	helm upgrade --install --reset-values prometheus prometheus-community/kube-prometheus-stack --set prometheus.prometheusSpec.podMonitorSelectorNilUsesHelmValues=false --wait
	kubectl wait Prometheus prometheus-kube-prometheus-prometheus --for=jsonpath='{.status.availableReplicas}'=1
	#kubectl wait sts prometheus-prometheus-kube-prometheus-prometheus --for=jsonpath='{.status.replicas}'=1
	touch _kind_deploy_prometheus

kind_deploy_pcm_with_prometheus:
	helm upgrade --install --reset-values pcm . --set podMonitor=true
	kubectl wait daemonset pcm --for=jsonpath='{.status.numberReady}'=1

kind_pcm_test_prometheus:
	kubectl proxy & sleep 10 && curl -sL http://127.0.0.1:8001/api/v1/namespaces/default/services/prometheus-kube-prometheus-prometheus:http-web/proxy/api/v1/query?query=Measurement_Interval_in_us | grep Measurement_Interval_in_us && kill %1

e2e-prometheus: _kind_deploy_cluster _kind_deploy_prometheus kind_deploy_pcm_with_prometheus kind_pcm_test kind_pcm_test_prometheus

#
# 4) e2e-metal-nfd: e2e thats tests that with node-feature-discovery installed and nfd values are changed, the PCM will be only installed on non hyperviserd system with Intel vendor and RDT available
#
_kind_deploy_nfd:
	#kubectl apply -k https://github.com/kubernetes-sigs/node-feature-discovery/deployment/overlays/default?ref=v0.16.0-devel
	helm repo add nfd https://kubernetes-sigs.github.io/node-feature-discovery/charts
	helm upgrade --install --wait nfd nfd/node-feature-discovery --namespace node-feature-discovery --create-namespace 
	# please be patient NFD requires around 2 minutes to annotate the node ...
	kubectl wait node --timeout=2m kind-control-plane --for=jsonpath='{.metadata.labels.feature\.node\.kubernetes\.io\/cpu-model\.vendor_id}'=Intel

kind_deploy_pcm_with_metal_nfd:
	helm upgrade --install --reset-values pcm . -f values-metal-nfd.yaml
	kubectl wait daemonset --timeout=2m pcm --for=jsonpath='{.status.numberReady}'=1

kind_pcm_test_nfd:
	kubectl wait daemonset pcm --timeout=2m --for=jsonpath='{.spec.template.spec.nodeSelector.feature\.node\.kubernetes\.io\/cpu-model\.vendor_id}'=Intel
	helm test pcm

e2e-metal-nfd: _kind_deploy_cluster _kind_deploy_nfd kind_deploy_pcm_with_metal_nfd kind_pcm_test

#
# 5) e2e-vpa: VPA E2E tests
#
_kind_autoscaler:
	git clone --depth 1 --single-branch https://github.com/kubernetes/autoscaler _kind_autoscaler

_kind_deploy_metrics_server:
	helm repo add metrics-server https://kubernetes-sigs.github.io/metrics-server/
	helm repo update
	helm upgrade --install --reset-values --set args={--kubelet-insecure-tls} metrics-server metrics-server/metrics-server --namespace kube-system
	kubectl wait daemonset pcm --for=jsonpath='{.status.numberReady}'=1
	touch _kind_deploy_metrics_server

_kind_deploy_vpa: autoscaler
	./_kind_autoscaler/vertical-pod-autoscaler/hack/vpa-up.sh
	touch _kind_deploy_vpa

kind_deploy_pcm_with_vpa:
	helm upgrade --install --reset-values pcm . --set verticalPodAutoscaler.enabled=true
	kubectl wait daemonset pcm --for=jsonpath='{.status.numberReady}'=1

e2e-vpa: _kind_deploy_cluster _kind_deploy_vpa kind_deploy_pcm_with_vpa kind_pcm_test  

#
# Cleanup
#

clean: 
	kind delete cluster
	docker rm -f kind-registry
	rm -fv _kind_with_registry.sh
	rm -fv _kind_extra_mounts.txt
	rm -fv _kind_with_registry.sh.tmp
	rm -fv _kind_deploy_cluster
	rm -fv _kind_deploy_prometheus
