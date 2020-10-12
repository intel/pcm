/*
Copyright (c) 2020, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <vector>
#include <memory>
#include <unistd.h>
#include "cpucounters.h"
#include "dashboard.h"

namespace pcm {

class Target
{
public:
    virtual std::string operator () (const std::string& refId) const = 0;
    virtual ~Target() {}
};

class InfluxDBTarget : public Target
{
    std::string alias;
    std::string metric;
    InfluxDBTarget() = delete;
public:
    InfluxDBTarget(const std::string & alias_, const std::string & metric_) : alias(alias_), metric(metric_) {}
    std::string operator () (const std::string & refId) const
    {
        std::string result;
        result += R"PCMDELIMITER(
        {
          "alias": ")PCMDELIMITER";
        result += alias;
        result += R"PCMDELIMITER(",
          "groupBy": [
            {
              "params": [
                "$__interval"
              ],
              "type": "time"
            },
            {
              "params": [
                "null"
              ],
              "type": "fill"
            }
          ],
          "measurement": "http",
          "orderByTime": "ASC",
          "policy": "default",
          "query": "SELECT )PCMDELIMITER";
        result += metric;
        result += R"PCMDELIMITER( FROM \"http\" WHERE $timeFilter GROUP BY time($__interval) fill(null)",
          "rawQuery": true,
          "refId": ")PCMDELIMITER";
        result += refId;
        result += R"PCMDELIMITER(",
          "resultFormat": "time_series",
          "select": [
            [
              {
                "params": [
                  "value"
                ],
                "type": "field"
              },
              {
                "params": [],
                "type": "mean"
              }
            ]
          ],
          "tags": []
        })PCMDELIMITER";
        return result;
    }
};

class PrometheusTarget : public Target
{
    std::string legend, expr;
    PrometheusTarget() = delete;
public:
    PrometheusTarget(const std::string& legend_, const std::string& expr_) : legend(legend_), expr(expr_) {}
    std::string operator () (const std::string& refId) const
    {
        std::string result;
        result += R"PCMDELIMITER(
        {
          "expr": ")PCMDELIMITER";
        result += expr;
        result += R"PCMDELIMITER(",
          "instant": false,
          "interval": "",
          "legendFormat": ")PCMDELIMITER";
        result += legend;
        result += R"PCMDELIMITER(",
          "refId": ")PCMDELIMITER";
        result += refId;
        result += R"PCMDELIMITER("
        })PCMDELIMITER";
        return result;
    }
};

class Panel
{
    int x, y, w, h;
    std::string title;
    std::vector<std::shared_ptr<Target>> targets;
    Panel() = delete;
protected:
    std::string getHeader(const int id) const
    {
        std::string result;
        result += R"PCMDELIMITER(
    {
      "datasource": null,
      "interval": "2s",
      "gridPos": {
)PCMDELIMITER";
        result += "        \"x\": " + std::to_string(x) + ",\n";
        result += "        \"y\": " + std::to_string(y) + ",\n";
        result += "        \"w\": " + std::to_string(w) + ",\n";
        result += "        \"h\": " + std::to_string(h);
        result += R"PCMDELIMITER(
      },
      "title": ")PCMDELIMITER";
        result += title;
        result += "\",\n      \"id\": " + std::to_string(id) + ",\n      \"targets\": [";
        char refId[] = "A";
        for (size_t i = 0; i< targets.size(); ++i, ++(refId[0]))
        {
            if (i > 0)
            {
                result += ",";
            }
            result += targets[i]->operator()(refId);
        }
        result += "\n      ],\n";
        return result;
    }
public:
    Panel(int x_, int y_, int w_, int h_, const std::string & title_) : x(x_), y(y_), w(w_), h(h_), title(title_) {}
    void push(const std::shared_ptr<Target> & t)
    {
        targets.push_back(t);
    }
    virtual std::string operator () (const int id) const = 0;
    virtual ~Panel() {}
};

class GaugePanel : public Panel
{
    GaugePanel() = delete;
public:
    GaugePanel(int x_, int y_, int w_, int h_, const std::string & title_) : Panel(x_, y_, w_, h_, title_) {}
    std::string operator () (const int id) const
    {
        std::string result = Panel::getHeader(id);
        result += R"PCMDELIMITER(      "options": {
        "fieldOptions": {
          "calcs": [
            "lastNotNull"
          ],
          "defaults": {
            "mappings": [],
            "max": 100,
            "min": 0,
            "thresholds": {
              "mode": "absolute",
              "steps": [
                {
                  "color": "green",
                  "value": null
                },
                {
                  "color": "red",
                  "value": 70
                }
              ]
            },
            "unit": "%"
          },
          "overrides": [],
          "values": false
        },
        "orientation": "auto",
        "showThresholdLabels": false,
        "showThresholdMarkers": true
      },
      "pluginVersion": "6.7.2",
      "timeFrom": null,
      "timeShift": null,
      "type": "gauge"
    })PCMDELIMITER";
        return result;
    }
};

class BarGaugePanel : public Panel
{
    BarGaugePanel() = delete;
public:
    BarGaugePanel(int x_, int y_, int w_, int h_, const std::string & title_) : Panel(x_, y_, w_, h_, title_) {}
    std::string operator () (const int id) const
    {
        std::string result = Panel::getHeader(id);
        result += R"PCMDELIMITER(      "cacheTimeout": null,
      "links": [
        {
          "title": "",
          "url": ""
        }
      ],
      "options": {
        "displayMode": "lcd",
        "fieldOptions": {
          "calcs": [
            "lastNotNull"
          ],
          "defaults": {
            "mappings": [
              {
                "$$hashKey": "object:413",
                "id": 0,
                "op": "=",
                "text": "N/A",
                "type": 1,
                "value": "null"
              }
            ],
            "nullValueMode": "connected",
            "thresholds": {
              "mode": "absolute",
              "steps": [
                {
                  "color": "green",
                  "value": null
                }
              ]
            },
            "unit": "none"
          },
          "overrides": [],
          "values": false
        },
        "orientation": "vertical",
        "showUnfilled": true
      },
      "pluginVersion": "6.7.2",
      "timeFrom": null,
      "timeShift": null,
      "type": "bargauge"
    })PCMDELIMITER";
        return result;
    }
};

class GraphPanel : public Panel
{
    std::string yAxisLabel;
    bool stack;
    GraphPanel() = delete;
public:
    GraphPanel(int x_, int y_, int w_, int h_, const std::string & title_, const std::string & yAxisLabel_, bool stack_)
        : Panel(x_, y_, w_, h_, title_)
        , yAxisLabel(yAxisLabel_)
        , stack(stack_)
    {
    }
    std::string operator () (const int id) const
    {
        std::string result = Panel::getHeader(id);
        result += R"PCMDELIMITER(      "aliasColors": {},
      "bars": false,
      "dashLength": 10,
      "dashes": false,
      "fill": 1,
      "fillGradient": 0,
      "hiddenSeries": false,
      "legend": {
        "avg": false,
        "current": false,
        "max": false,
        "min": false,
        "show": true,
        "total": false,
        "values": false
      },
      "lines": true,
      "linewidth": 1,
      "links": [
        {
          "title": "",
          "url": ""
        }
      ],
      "nullPointMode": "null",
      "options": {
        "dataLinks": []
      },
      "percentage": false,
      "pluginVersion": "6.7.2",
      "pointradius": 2,
      "points": false,
      "renderer": "flot",
      "seriesOverrides": [],
      "spaceLength": 10,
      "stack": )PCMDELIMITER";
      result += stack? "true" : "false";
      result += R"PCMDELIMITER(,
      "steppedLine": false,
      "thresholds": [],
      "timeFrom": null,
      "timeRegions": [],
      "timeShift": null,
      "tooltip": {
        "shared": true,
        "sort": 0,
        "value_type": "individual"
      },
      "type": "graph",
      "xaxis": {
        "buckets": null,
        "mode": "time",
        "name": null,
        "show": true,
        "values": []
      },
      "yaxes": [
        {
          "$$hashKey": "object:2758",
          "format": "none",
          "label": ")PCMDELIMITER";
          result += yAxisLabel;
          result += R"PCMDELIMITER(",
          "logBase": 1,
          "max": null,
          "min": "0",
          "show": true
        },
        {
          "$$hashKey": "object:2759",
          "format": "none",
          "label": null,
          "logBase": 1,
          "max": null,
          "min": null,
          "show": true
        }
      ],
      "yaxis": {
        "align": false,
        "alignLevel": null
      }
    })PCMDELIMITER";
        return result;
    }
};

class Dashboard
{
    std::string title;
    std::vector<std::shared_ptr<Panel>> panels;
    Dashboard() = delete;
public:
    Dashboard(const std::string & title_) : title(title_) {}
    void push(const std::shared_ptr<Panel> & p)
    {
        panels.push_back(p);
    }
    std::string operator () () const
    {
        std::string result;
        result += R"PCMDELIMITER({
  "annotations": {
    "list": [
      {
        "$$hashKey": "object:2661",
        "builtIn": 1,
        "datasource": "-- Grafana --",
        "enable": true,
        "hide": true,
        "iconColor": "rgba(0, 211, 255, 1)",
        "name": "Annotations & Alerts",
        "type": "dashboard"
      }
    ]
  },
  "editable": true,
  "gnetId": null,
  "graphTooltip": 0,
  "id": 1,
  "links": [],
  "panels": [)PCMDELIMITER";
        for (size_t i=0; i < panels.size(); ++i)
        {
            if (i > 0)
            {
                result += ",";
            }
            result += panels[i]->operator()(i + 2);
        }
        result += R"PCMDELIMITER(
  ],
  "refresh": "5s",
  "schemaVersion": 22,
  "style": "dark",
  "tags": [],
  "templating": {
    "list": []
  },
  "time": {
    "from": "now-5m",
    "to": "now"
  },
  "timepicker": {},
  "timezone": "",
  "title": ")PCMDELIMITER";
        result += title;
        result += R"PCMDELIMITER(",
  "uid": "A_CvwTCWk",
  "variables": {
    "list": []
  },
  "version": 1
})PCMDELIMITER";
        return result;
    }
};

std::string prometheusMetric(const std::string& m)
{
    auto result = m;
    for (char& c : result)
    {
        if (c == ' ' || c == '-')
        {
            c = '_';
        }
    }
    return result;
}

std::string prometheusSystem()
{
    return "{aggregate=\\\"system\\\"}";
}

std::string prometheusSocket(const std::string& S, const bool aggregate = true)
{
    if (aggregate)
        return "{aggregate=\\\"socket\\\", socket=\\\"" + S + "\\\"}";
    return "{socket=\\\"" + S + "\\\"}";
}

std::string prometheusSystem(const std::string& S)
{
    return "{aggregate=\\\"system\\\", socket=\\\"" + S + "\\\"}";
}

std::string influxDB_Counters(const std::string& S, const std::string& m, const char * domain)
{
    return std::string("mean(\\\"Sockets_") + S + "_" + domain + "_" + m + "\\\")";
}

std::string influxDB_Counters(const std::string& m, const char* domain)
{
    return std::string("mean(\\\"") + domain + "_" + m + "\\\")";
}

std::string influxDBCore_Aggregate_Core_Counters(const std::string& S, const std::string& m)
{
    return influxDB_Counters(S, m, "Core Aggregate_Core Counters");
}

std::string influxDBCore_Aggregate_Core_Counters(const std::string& m)
{
    return influxDB_Counters(m, "Core Aggregate_Core Counters");
}

std::string influxDBUncore_Uncore_Counters(const std::string& S, const std::string& m)
{
    return influxDB_Counters(S, m, "Uncore_Uncore Counters");
}

constexpr const char* interval = "[4s]";

std::string prometheusCounters(const std::string& S, const std::string& m, const bool aggregate = true)
{
    return std::string("rate(") + prometheusMetric(m) + prometheusSocket(S, aggregate) + interval + ")";
}

std::string prometheusCounters(const std::string& m)
{
    return std::string("rate(") + prometheusMetric(m) + prometheusSystem() + interval + ")";
}

std::string getPCMDashboardJSON(const PCMDashboardType type, int ns, int nu, int nc)
{
    auto pcm = PCM::getInstance();
    const size_t NumSockets = (ns < 0) ? pcm->getNumSockets() : ns;
    const size_t NumUPILinksPerSocket = (nu < 0) ? pcm->getQPILinksPerSocket() : nu;
    const size_t maxCState = (nc < 0) ? PCM::MAX_C_STATE : nc;

    const int height = 5;
    const int width = 15;
    const int max_width = 24;
    int y = 0;
    char buffer[64];
    std::string hostname = "unknown hostname";
    if (gethostname(buffer, 63) == 0)
    {
        hostname = buffer;
    }
    Dashboard dashboard("Processor Counter Monitor (PCM) Dashboard - " + hostname);
    auto createTarget = [type](const std::string& title, const std::string& inluxdbMetric, const std::string& prometheusExpr) -> std::shared_ptr<Target>
    {
        std::shared_ptr<Target> t;
        if (type == InfluxDB)
            t = std::make_shared<InfluxDBTarget>(title, inluxdbMetric);
        else
            t = std::make_shared<PrometheusTarget>(title, prometheusExpr);
        return t;
    };
    {
        auto panel = std::make_shared<GraphPanel>(0, y, width, height, "Memory Bandwidth", "MByte/sec", false);
        auto panel1 = std::make_shared<BarGaugePanel>(width, y, max_width - width, height, "Memory Bandwidth (MByte/sec)");
        y += height;
        auto genAll = [type](const std::string& special) -> std::string
        {
            std::string all;
            for (auto& m : { "DRAM Reads", "DRAM Writes", "Persistent Memory Reads", "Persistent Memory Writes" })
            {
                if (all.size() > 0)
                {
                    all += " + ";
                }
                if (type == InfluxDB)
                    all += special + "_Uncore Counters_" + m + "\\\")/1048576";
                else
                    all += std::string("rate(") + prometheusMetric(m) + special + interval + ")/1048576";
            }
            return all;
        };
        for (size_t s = 0; s < NumSockets; ++s)
        {
            const auto S = std::to_string(s);
            auto t = createTarget("Socket" + S, genAll("mean(\\\"Sockets_" + S + "_Uncore"), genAll(prometheusSocket(S, false)));
            panel->push(t);
            panel1->push(t);
        }
        auto t = createTarget("Total", genAll("mean(\\\"Uncore Aggregate"), genAll(prometheusSystem()));
        panel->push(t);
        panel1->push(t);
        dashboard.push(panel);
        dashboard.push(panel1);
    };
    for (size_t s = 0; s < NumSockets; ++s)
    {
        const auto S = std::to_string(s);
        auto panel = std::make_shared<GraphPanel>(0, y, width, height, std::string("Socket") + S + " Memory Bandwidth", "MByte/sec", false);
        auto panel1 = std::make_shared<BarGaugePanel>(width, y, max_width - width, height, std::string("Current Socket") + S + " Memory Bandwidth (MByte/sec)");
        y += height;
        for (auto& m : { "DRAM Reads", "DRAM Writes", "Persistent Memory Reads", "Persistent Memory Writes" })
        {
            auto t = createTarget(m, influxDBUncore_Uncore_Counters(S, m) + "/1048576", prometheusCounters(S, m, false) + "/1048576");
            panel->push(t);
            panel1->push(t);
        }
        dashboard.push(panel);
        dashboard.push(panel1);
    }
    auto upi = [&](const std::string & m, const bool utilization)
    {
        for (size_t s = 0; s < NumSockets; ++s)
        {
            const auto S = std::to_string(s);
            auto panel = std::make_shared<GraphPanel>(0, y, width, height, std::string("Socket") + S + " UPI " + m, utilization?"%": "MByte/sec", false);
            std::shared_ptr<Panel> panel1;
            if (utilization)
                panel1 = std::make_shared<GaugePanel>(width, y, max_width - width, height, std::string("Current Socket") + S + " UPI " + m + " (%)");
            else
                panel1 = std::make_shared<BarGaugePanel>(width, y, max_width - width, height, std::string("Current Socket") + S + " UPI " + m + " (MByte/sec)");
            y += height;
            const auto suffix = utilization ? "*100" : "/1048576";
            for (size_t l = 0; l < NumUPILinksPerSocket; ++l)
            {
                const auto L = std::to_string(l);
                auto t = createTarget("UPI" + std::to_string(l),
                    "mean(\\\"QPI/UPI Links_QPI Counters Socket " + S + "_" + m + " On Link " + L + "\\\")" +  suffix,
                    "rate(" + prometheusMetric(m) + "_On_Link_" + L + prometheusSystem(S) + interval + ")" + suffix);
                panel->push(t);
                panel1->push(t);
            }
            dashboard.push(panel);
            dashboard.push(panel1);
        }
    };
    for (auto &m : {"Utilization Outgoing Data And Non-Data Traffic", "Utilization Incoming Data Traffic"})
    {
        upi(m, true);
    }
    for (auto & m : {"Outgoing Data And Non-Data Traffic", "Incoming Data Traffic"})
    {
        upi(m, false);
    }
    auto cstate = [&] (const char * m, const char * tPrefix, const char * source)
    {
        auto my_height = 3 * height / 2;
        auto panel = std::make_shared<GraphPanel>(0, y, width, my_height, std::string(m) + " C-state residency", "stacked %", true);
        auto panel1 = std::make_shared<BarGaugePanel>(width, y, max_width - width, my_height, std::string("Current ") + m + " C-state residency (%)");
        y += my_height;
        auto prometheusCStateExpression = [](const std::string& source, const size_t c) -> std::string
        {
            auto C = std::to_string(c);
            return std::string("100 * rate(RawCStateResidency{ aggregate = \\\"system\\\", index = \\\"") + C + "\\\", source = \\\"" + source + "\\\" }" + interval +
                ") / ignoring(source, index) rate(Invariant_TSC{ aggregate = \\\"system\\\" }" + interval + ")";
        };
        auto prometheusComputedCStateExpression = [&maxCState, &prometheusCStateExpression](const std::string& source, const size_t e) -> std::string
        {
            std::string result = "100";
            for (size_t c = 0; c < maxCState + 1; ++c)
            {
                if (e != c)
                {
                    result = result + " - (" + prometheusCStateExpression(source, c) + ") ";
                }
            }
            return result;
        };
        for (size_t c = 0; c < maxCState + 1; ++c)
        {
            auto C = std::to_string(c);
            auto pExpr = prometheusCStateExpression(source, c);
            if ((std::string(source) == "core" && c == 1) || (std::string(source) == "uncore" && c == 0))
            {
               pExpr = prometheusComputedCStateExpression(source, c);
            }
            auto t = createTarget("C" + C, std::string("mean(\\\"") + tPrefix + " Counters_CStateResidency[" + C + "]\\\")*100", pExpr);
            panel->push(t);
            panel1->push(t);
        }
        dashboard.push(panel);
        dashboard.push(panel1);
    };
    cstate("Core", "Core Aggregate_Energy", "core");
    cstate("Package", "Uncore Aggregate_Uncore", "uncore");
    auto derived = [&](const std::string & fullName, const std::string & shortName, const std::string & dividend, const std::string & divisor)
    {
        auto panel = std::make_shared<GraphPanel>(0, y, width, height, fullName, shortName, false);
        auto panel1 = std::make_shared<BarGaugePanel>(width, y, max_width - width, height, fullName);
        y += height;
        for (size_t s = 0; s < NumSockets; ++s)
        {
            const auto S = std::to_string(s);
            auto t = createTarget("Socket" + S,
                influxDBCore_Aggregate_Core_Counters(S, dividend) + "/" + influxDBCore_Aggregate_Core_Counters(S, divisor),
                prometheusCounters(S, dividend) + "/" + prometheusCounters(S, divisor));
            panel->push(t);
            panel1->push(t);
        }
        auto t = createTarget("Total",
            influxDBCore_Aggregate_Core_Counters(dividend) + "/" + influxDBCore_Aggregate_Core_Counters(divisor),
            prometheusCounters(dividend) + "/" + prometheusCounters(divisor)
        );
        panel->push(t);
        panel1->push(t);
        dashboard.push(panel);
        dashboard.push(panel1);
    };
    derived("Instructions Per Cycle", "IPC", "Instructions Retired Any", "Clock Unhalted Thread");
    derived("Active Frequency Ratio", "AFREQ", "Clock Unhalted Thread", "Clock Unhalted Ref");
    derived("L3 Cache Misses Per Instruction", "L3 MPI", "L3 Cache Misses", "Instructions Retired Any");
    derived("L2 Cache Misses Per Instruction", "L2 MPI", "L2 Cache Misses", "Instructions Retired Any");
    for (auto & m : {"Instructions Retired Any", "Clock Unhalted Thread", "L2 Cache Hits", "L2 Cache Misses", "L3 Cache Hits", "L3 Cache Misses"})
    {
        auto panel = std::make_shared<GraphPanel>(0, y, width, height, std::string(m), "Million", false);
        auto panel1 = std::make_shared<BarGaugePanel>(width, y, max_width - width, height, std::string(m) + " (Million)");
        y += height;
        for (size_t s = 0; s < NumSockets; ++s)
        {
            const auto S = std::to_string(s);
            auto t = createTarget("Socket" + S, influxDBCore_Aggregate_Core_Counters(S, m) + "/1000000", prometheusCounters(S, m) + "/1000000");
            panel->push(t);
            panel1->push(t);
        }
        auto t = createTarget("Total", influxDBCore_Aggregate_Core_Counters(m) + "/1000000", prometheusCounters(m) + "/1000000");
        panel->push(t);
        panel1->push(t);
        dashboard.push(panel);
        dashboard.push(panel1);
    }
    for (size_t s = 0; s < NumSockets; ++s)
    {
        const auto S = std::to_string(s);
        auto panel = std::make_shared<GraphPanel>(0, y, width, height, std::string("Socket") +  S + " Energy Consumption", "Watt", false);
        auto panel1 = std::make_shared<BarGaugePanel>(width, y, max_width - width, height, std::string("Current Socket") +  S + " Energy Consumption (Watt)");
        y += height;
        for (auto &m : {"Package Joules Consumed", "DRAM Joules Consumed"})
        {
          auto t = createTarget(m, influxDBUncore_Uncore_Counters(S, m), prometheusCounters(S, m, false));
          panel->push(t);
          panel1->push(t);
        }
        dashboard.push(panel);
        dashboard.push(panel1);
    }
    return dashboard();
}

} // namespace pcm
