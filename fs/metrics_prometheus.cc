#include "metrics_prometheus.h"

#include <prometheus/collectable.h>
#include <prometheus/counter.h>
#include <prometheus/registry.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>

using namespace ROCKSDB_NAMESPACE;
using namespace prometheus;

ZenFSPrometheusMetrics::ZenFSPrometheusMetrics() {
  registry_ = std::make_shared<Registry>();

  for (auto &label_with_type : info_map_)
    AddReporter(static_cast<uint32_t>(label_with_type.first),
                static_cast<uint32_t>(label_with_type.second.second));

  stop_collect_thread_.store(false);
  collect_thread_ = new std::thread(&ZenFSPrometheusMetrics::run, this);
}

ZenFSPrometheusMetrics::~ZenFSPrometheusMetrics() {
  stop_collect_thread_.store(true);
  collect_thread_->join();
}

void ZenFSPrometheusMetrics::run() {
  Exposer server{"127.0.0.1:8080"};
  server.RegisterCollectable(registry_);

  auto wake = std::chrono::steady_clock::now() +
              std::chrono::milliseconds(this->report_interval_ms_);
  while (!stop_collect_thread_.load()) {
    std::this_thread::sleep_until(wake);
    wake = wake + std::chrono::milliseconds(this->report_interval_ms_);
    for (auto &metric : metric_map_) {
      auto gm = metric.second;

      // Handle concurrency by atomic exchange. We don't care about inaccuracy
      // caused by counters not being swapped atomically all at once.
      gm->gcount->Set(metric.second->count.exchange(0));
      gm->gtotal->Set(metric.second->value.exchange(0));
      gm->gmin->Set(metric.second->min.exchange(UINT64_MAX));
      gm->gmax->Set(metric.second->max.exchange(0));
    }
  }
}

void ZenFSPrometheusMetrics::Report(uint32_t label_uint, size_t value,
                                    uint32_t type_uint) {
  auto label = static_cast<ZenFSMetricsHistograms>(label_uint);

  if (metric_map_.find(label) == metric_map_.end()) return;

  auto metric = metric_map_.find(label)->second;
  metric->value += value;
  metric->count++;

  auto max = metric->max.load();
  while (1) {
    if (value > max) {
      if (metric->max.compare_exchange_weak(max, value)) {
        break;
      }
    } else {
      break;
    }
  }

  auto min = metric->min.load();
  while (1) {
    if (value < min) {
      if (metric->min.compare_exchange_weak(min, value)) {
        break;
      }
    } else {
      break;
    }
  }
}

void ZenFSPrometheusMetrics::AddReporter(uint32_t label_uint,
                                         ReporterType type) {
  auto label = static_cast<ZenFSMetricsHistograms>(label_uint);

  assert(info_map_.find(label) != info_map_.end());
  if (info_map_.find(label) == info_map_.end()) return;

  auto pair = info_map_.find(label)->second;
  auto name = pair.first;

  auto metric = std::make_shared<GaugeMetric>();

  metric->family = &BuildGauge().Name(name).Register(*registry_);

  metric->gmax = &metric->family->Add({{"type", "max"}});
  metric->gmin = &metric->family->Add({{"type", "min"}});
  metric->gcount = &metric->family->Add({{"type", "count"}});
  metric->gtotal = &metric->family->Add({{"type", "total"}});

  metric->min = UINT64_MAX;

  metric_map_.emplace(label, metric);
}
