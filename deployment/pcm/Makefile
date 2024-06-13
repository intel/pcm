# https://stackoverflow.com/questions/649246/is-it-possible-to-create-a-multi-line-string-variable-in-a-makefile
define KIND_EXTRA_MOUNTS
nodes:
- role: control-plane
  extraMounts:
  - hostPath: /sys/fs/resctrl
    containerPath: /sys/fs/resctrl
endef

chart-lint-report.txt: values.yaml templates
	docker run -ti --rm -w /pcm -v `realpath $(PWD)/../..`:/pcm quay.io/helmpack/chart-testing ct lint --charts deployment/pcm --validate-maintainers=false | tee chart-lint-report.txt

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


clean: 
	kind delete cluster
	docker rm -f kind-registry
	rm -fv _kind_with_registry.sh
	rm -fv _kind_extra_mounts.txt
	rm -fv _kind_with_registry.sh.tmp
	rm -fv _kind_deploy_cluster
	rm -fv _kind_deploy_prometheus
	rm -fv _kind_deploy_pcm


#
# e2e-small: minimal E2e pcm pod only test
#
_kind_deploy_pcm:
	helm install pcm .
	touch _kind_deploy_pcm

kind_pcm_test:
	helm test pcm

e2e-small: _kind_deploy_cluster _kind_deploy_pcm kind_pcm_test

#
# e2e-small-metal-nfd: minimal E2e pcm pod only test but with direct approach
#
_kind_deploy_pcm_metal:
	helm upgrade --install pcm . -f values-metal.yaml
	touch _kind_deploy_pcm_metal

_kind_deploy_nfd:
	helm upgrade --install pcm . -f values-metal.yaml
	touch _kind_deploy_pcm_metal

kind_pcm_test:
	helm test pcm

e2e-small-metal: _kind_deploy_cluster _kind_deploy_pcm_metal kind_pcm_test

#
# e2e-prometheus: E2E test for podMonitor (pod monitor test)
#
_kind_deploy_prometheus:
	helm repo add prometheus-community https://prometheus-community.github.io/helm-charts
	helm upgrade --install prometheus prometheus-community/kube-prometheus-stack --set prometheus.prometheusSpec.podMonitorSelectorNilUsesHelmValues=false
	kubectl wait sts prometheus-prometheus-kube-prometheus-prometheus --for=jsonpath='{.status.replicas}'=1
	touch _kind_deploy_prometheus

_kind_deploy_pcm-with-prometheus:
	helm upgrade --install pcm . --set podMonitor=true

kind_pcm_prometheus_test:
	kubectl proxy & sleep 5 && curl -sL http://127.0.0.1:8001/api/v1/namespaces/default/services/prometheus-kube-prometheus-prometheus:http-web/proxy/api/v1/query?query=Measurement_Interval_in_us | grep Measurement_Interval_in_us && kill %1

e2e-prometheus: _kind_deploy_cluster _kind_deploy_prometheus _kind_deploy_pcm-with-prometheus kind_pcm_test kind_pcm_prometheus_test

#
# e2e-vpa: VPA E2E tests
#
_kind_autoscaler:
	git clone --depth 1 --single-branch https://github.com/kubernetes/autoscaler _kind_autoscaler

_kind_deploy_metrics_server:
	helm repo add metrics-server https://kubernetes-sigs.github.io/metrics-server/
	helm repo update
	helm upgrade --install --set args={--kubelet-insecure-tls} metrics-server metrics-server/metrics-server --namespace kube-system
	touch _kind_deploy_metrics_server

_kind_deploy_vpa: autoscaler
	./_kind_autoscaler/vertical-pod-autoscaler/hack/vpa-up.sh
	touch _kind_deploy_vpa

_kind_deploy_pcm_with_vpa:
	helm upgrade --install pcm . --set verticalPodAutoscaler.enabled=true

e2e-vpa: _kind_deploy_cluster _kind_deploy_vpa _kind_deploy_pcm_with_vpa kind_pcm_test  
