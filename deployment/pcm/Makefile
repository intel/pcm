chart-lint-report.txt: values.yaml templates
	docker run -ti --rm -w /pcm -v `realpath $(PWD)/../..`:/pcm quay.io/helmpack/chart-testing ct lint --charts deployment/pcm --validate-maintainers=false | tee chart-lint-report.txt
