.PHONY=kind-cluster-clean

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
kind-with-registry.sh:
	curl -sl https://kind.sigs.k8s.io/examples/kind-with-registry.sh -o kind-with-registry.sh.tmp
	echo "$$KIND_EXTRA_MOUNTS" >kind_extra_mounts.txt
	sed '/apiVersion: kind.x-k8s.io\/v1alpha4/r kind_extra_mounts.txt' kind-with-registry.sh.tmp >kind-with-registry.sh
	chmod +x kind-with-registry.sh

kind-cluster: kind-with-registry.sh
	./kind-with-registry.sh
	kind export kubeconfig
	touch kind-cluster


clean: 
	kind delete cluster
	docker rm -f kind-registry
	rm -fv kind_extra_mounts.txt
	rm -fv kind-with-registry.sh
	rm -fv kind-with-registry.sh.tmp
	rm -fv kind-deploy-prometheus


#
# minimal
#
kind-deploy-pcm:
	helm install pcm .

kind-pcm-test:
	helm test pcm

e2e-small: kind-cluster kind-deploy-pcm kind-pcm-test


#
# prometheus (pod monitor test)
#
kind-deploy-prometheus:
	helm repo add prometheus-community https://prometheus-community.github.io/helm-charts
	helm upgrade --install prometheus prometheus-community/kube-prometheus-stack --set prometheus.prometheusSpec.podMonitorSelectorNilUsesHelmValues=false
	kubectl wait sts prometheus-prometheus-kube-prometheus-prometheus --for=jsonpath='{.status.replicas}'=1
	touch kind-deploy-prometheus

kind-pcm-upgrade-prometheus:
	helm upgrade --install pcm . --set podMonitor=true

kind-pcm-prometheus-test:
	kubectl proxy & sleep 5 && curl -sL http://127.0.0.1:8001/api/v1/namespaces/default/services/prometheus-kube-prometheus-prometheus:http-web/proxy/api/v1/query?query=Measurement_Interval_in_us | grep Measurement_Interval_in_us && kill %1

e2e-prometheus: kind-cluster kind-deploy-prometheus kind-pcm-upgrade-prometheus kind-pcm-test kind-pcm-prometheus-test

#
# VPA
#
autoscaler:
	git clone --depth 1 --single-branch https://github.com/kubernetes/autoscaler

kind-deploy-metrics-server:
	helm repo add metrics-server https://kubernetes-sigs.github.io/metrics-server/
	helm repo update
	helm upgrade --install --set args={--kubelet-insecure-tls} metrics-server metrics-server/metrics-server --namespace kube-system

kind-deploy-vpa: autoscaler
	./autoscaler/vertical-pod-autoscaler/hack/vpa-up.sh

kind-pcm-vpa:
	helm upgrade --install pcm . --set verticalPodAutoscaler.enabled=true

e2e-vpa: kind-cluster kind-deploy-prometheus kind-deploy-vpa kind-pcm-test  
