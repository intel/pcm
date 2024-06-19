{{/* Expand the name of the chart.  */}}
{{- define "pcm.name" -}}
{{- default .Chart.Name .Values.nameOverride | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/* Create a default fully qualified app name.
We truncate at 63 chars because some Kubernetes name fields are limited to this (by the DNS naming spec).
If release name contains chart name it will be used as a full name.  */}}
{{- define "pcm.fullname" -}}
{{- if .Values.fullnameOverride }}
{{- .Values.fullnameOverride | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- $name := default .Chart.Name .Values.nameOverride }}
{{- if contains $name .Release.Name }}
{{- .Release.Name | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- printf "%s-%s" .Release.Name $name | trunc 63 | trimSuffix "-" }}
{{- end }}
{{- end }}
{{- end }}

{{/* Create chart name and version as used by the chart label.  */}}
{{- define "pcm.chart" -}}
{{- printf "%s-%s" .Chart.Name .Chart.Version | replace "+" "_" | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/* Selector labels */}}
{{- define "pcm.selectorLabels" -}}
app.kubernetes.io/name: {{ include "pcm.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
app.kubernetes.io/component: pcm-sensor-server
{{- end }}

{{/* Common labels */}}
{{- define "pcm.labels" -}}
helm.sh/chart: {{ include "pcm.chart" . }}
{{ include "pcm.selectorLabels" . }}
{{- if .Chart.AppVersion }}
app.kubernetes.io/version: {{ .Chart.AppVersion | quote }}
{{- end }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
{{- end }}

{{/* SecurityContext privileged or capabilties */}}
{{- define "pcm.securityContext" -}}
securityContext:
{{- if .Values.privileged }}
  privileged: true
{{- else -}}
  {{/* TODO?
  readOnlyRootFilesystem: false
  runAsUser: 0
  runAsGroup: 0
  ## below two doesnt work on container level!
  fsGroup: 0
  supplementalGroups: [0]
  seccompProfile:
    #type: RuntimeDefault
    type: Unconfined
  */}}
  capabilities:
    add:
    - {{ if .Values.cap_perfmon }}PERFMON{{ else }}SYS_ADMIN{{ end }} 
    - SYS_RAWIO
{{- end }}
{{- end }}


{{/* Probes: liveness and readiness probe */}}
{{- define "pcm.probe" -}}
failureThreshold: 3
httpGet:
  path: /
  port: 9738
  scheme: HTTP
periodSeconds: 10
successThreshold: 1
timeoutSeconds: 1
{{- end }}
