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
#include "cpucounters.h"
#include "dashboard.h"

class Target
{
    std::string alias;
    std::string metric;
    Target() = delete;
public:
    Target(const std::string & alias_, const std::string & metric_) : alias(alias_), metric(metric_) {}
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
          "min": null,
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

std::string getPCMDashboardJSON(int ns, int nu, int nc)
{
    auto pcm = PCM::getInstance();
    const size_t NumSockets = (ns < 0) ? pcm->getNumSockets() : ns;
    const size_t NumUPILinksPerSocket = (nu < 0) ? pcm->getQPILinksPerSocket() : nu;
    const size_t maxCState = (nc < 0) ? PCM::MAX_C_STATE : nc;

    const int height = 5;
    const int width = 15;
    const int max_width = 24;
    int y = 0;
    Dashboard dashboard("Processor Counter Monitor (PCM) Dashboard");
    {
        auto panel = std::make_shared<GraphPanel>(0, y, width, height, "Memory Bandwidth", "MByte/sec", false);
        auto panel1 = std::make_shared<BarGaugePanel>(width, y, max_width - width, height, "Memory Bandwidth (MByte/sec)");
        y += height;
        auto genAll = [](const std::string &prefix) -> std::string
        {
          std::string all;
          for (auto &m : {"DRAM Reads", "DRAM Writes", "Persistent Memory Reads", "Persistent Memory Writes"})
          {
            if (all.size() > 0)
            {
              all += " + ";
            }
            all += prefix + "_Uncore Counters_" + m + "\\\")/1048576";
          }
          return all;
        };
        for (size_t s = 0; s < NumSockets; ++s)
        {
            const auto S = std::to_string(s);
            auto t = std::make_shared<Target>("Socket" + S, genAll("mean(\\\"Sockets_" + S + "_Uncore"));
            panel->push(t);
            panel1->push(t);
        }
        auto t = std::make_shared<Target>("Total", genAll("mean(\\\"Uncore Aggregate"));
        panel->push(t);
        panel1->push(t);
        dashboard.push(panel);
        dashboard.push(panel1);
    };
    for (size_t s = 0; s < NumSockets; ++s)
    {
        const auto S = std::to_string(s);
        auto panel = std::make_shared<GraphPanel>(0, y, width, height, std::string("Socket") +  S + " Memory Bandwidth", "MByte/sec", false);
        auto panel1 = std::make_shared<BarGaugePanel>(width, y, max_width - width, height, std::string("Current Socket") +  S + " Memory Bandwidth (MByte/sec)");
        y += height;
        for (auto &m : {"DRAM Reads", "DRAM Writes", "Persistent Memory Reads", "Persistent Memory Writes"})
        {
          auto t = std::make_shared<Target>(m, "mean(\\\"Sockets_" + S + "_Uncore_Uncore Counters_" + m + "\\\")/1048576");
          panel->push(t);
          panel1->push(t);
        }
        dashboard.push(panel);
        dashboard.push(panel1);
    }
    for (auto &m : {"Utilization Outgoing Data And Non-Data Traffic", "Utilization Incoming Data Traffic"})
    {
        for (size_t s = 0; s < NumSockets; ++s)
        {
            const auto S = std::to_string(s);
            auto panel = std::make_shared<GraphPanel>(0, y, width, height, std::string("Socket") + S + " UPI " + m, "%", false);
            auto panel1 = std::make_shared<GaugePanel>(width, y, max_width - width, height, std::string("Current Socket") + S + " UPI " + m + " (%)");
            y += height;
            for (size_t l = 0; l < NumUPILinksPerSocket; ++l)
            {
                auto t = std::make_shared<Target>("UPI" + std::to_string(l),
                                                  "mean(\\\"QPI/UPI Links_QPI Counters Socket " + S + "_" + m + " On Link " + std::to_string(l) + "\\\")*100");
                panel->push(t);
                panel1->push(t);
            }
            dashboard.push(panel);
            dashboard.push(panel1);
        }
    }
    for (auto & m : {"Outgoing Data And Non-Data Traffic", "Incoming Data Traffic"})
    {
        for (size_t s = 0; s < NumSockets; ++s)
        {
            const auto S = std::to_string(s);
            auto panel = std::make_shared<GraphPanel>(0, y, width, height, std::string("Socket") + S + " UPI " + m, "MByte/sec", false);
            auto panel1 = std::make_shared<BarGaugePanel>(width, y, max_width - width, height, std::string("Current Socket") + S + " UPI " + m + " (MByte/sec)");
            y += height;
            for (size_t l = 0; l < NumUPILinksPerSocket; ++l)
            {
                auto t = std::make_shared<Target>("UPI" + std::to_string(l),
                                                  "mean(\\\"QPI/UPI Links_QPI Counters Socket " + S + "_" + m + " On Link " + std::to_string(l) + "\\\")/1048576");
                panel->push(t);
                panel1->push(t);
            }
            dashboard.push(panel);
            dashboard.push(panel1);
        }
    }
    auto cstate = [&] (const char * m, const char * tPrefix)
    {
        auto my_height = 3 * height / 2;
        auto panel = std::make_shared<GraphPanel>(0, y, width, my_height, std::string(m) + " C-state residency", "stacked %", true);
        auto panel1 = std::make_shared<BarGaugePanel>(width, y, max_width - width, my_height, std::string("Current ") + m + " C-state residency (%)");
        y += my_height;
        for (size_t c = 0; c < maxCState + 1; ++c)
        {
            auto C = std::to_string(c);
            auto t = std::make_shared<Target>("C" + C,
                                              std::string("mean(\\\"") + tPrefix + " Counters_CStateResidency[" + C + "]\\\")*100");
            panel->push(t);
            panel1->push(t);
        }
        dashboard.push(panel);
        dashboard.push(panel1);
    };
    cstate("Core", "Core Aggregate_Energy");
    cstate("Package", "Uncore Aggregate_Uncore");
    auto derived = [&](const std::string & fullName, const std::string & shortName, const std::string & dividend, const std::string & divisor)
    {
        auto panel = std::make_shared<GraphPanel>(0, y, width, height, fullName, shortName, false);
        auto panel1 = std::make_shared<BarGaugePanel>(width, y, max_width - width, height, fullName);
        y += height;
        for (size_t s = 0; s < NumSockets; ++s)
        {
            const auto S = std::to_string(s);
            auto t = std::make_shared<Target>("Socket" + S,
                                                      std::string("mean(\\\"Sockets_") + S + "_Core Aggregate_Core Counters_" + dividend + "\\\")" +
                                                      "/" +
                                                      std::string("mean(\\\"Sockets_") + S + "_Core Aggregate_Core Counters_" + divisor + "\\\")");
            panel->push(t);
            panel1->push(t);
        }
        auto t = std::make_shared<Target>("Total", std::string("mean(\\\"Core Aggregate_Core Counters_" + dividend + "\\\")") +
                                                   "/" +
                                                   "mean(\\\"Core Aggregate_Core Counters_" + divisor + "\\\")");
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
            auto t = std::make_shared<Target>("Socket" + S,
                                                      std::string("mean(\\\"Sockets_") + S + "_Core Aggregate_Core Counters_" + m + "\\\")/1000000");
            panel->push(t);
            panel1->push(t);
        }
        auto t = std::make_shared<Target>("Total",
                                                      std::string("mean(\\\"Core Aggregate_Core Counters_") + m + "\\\")/1000000");
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
        for (auto &m : {"Package", "DRAM"})
        {
          auto t = std::make_shared<Target>(m, "mean(\\\"Sockets_" + S + "_Uncore_Uncore Counters_" + m + " Joules Consumed\\\")");
          panel->push(t);
          panel1->push(t);
        }
        dashboard.push(panel);
        dashboard.push(panel1);
    }
    return dashboard();
}