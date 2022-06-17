#pragma once

#include <prometheus/counter.h>
#include <prometheus/exposer.h>
#include <prometheus/registry.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include "metrics.h"

class ResettingGauge;

namespace ROCKSDB_NAMESPACE {

using namespace prometheus;

class GaugeMetric {
 public:
  Family<Gauge> *family;
  Gauge *gmin;
  Gauge *gmax;
  Gauge *gtotal;
  Gauge *gcount;
  std::atomic<uint64_t> value;
  std::atomic<uint64_t> count;
  std::atomic<uint64_t> max;
  std::atomic<uint64_t> min;
};

class ZenFSPrometheusMetrics : public rocksdb::ZenFSMetrics {
 private:
  std::shared_ptr<Registry> registry_;
  std::unordered_map<ZenFSMetricsHistograms, std::shared_ptr<GaugeMetric>>
      metric_map_;
  uint64_t report_interval_ms_ = 5000;
  std::thread *collect_thread_;
  std::atomic_bool stop_collect_thread_;

  const std::unordered_map<uint32_t, std::pair<std::string, uint32_t>>
      info_map_ = {
          {ZENFS_NON_WAL_WRITE_LATENCY,
           {"zenfs_non_wal_write_latency", ZENFS_REPORTER_TYPE_LATENCY}},
          {ZENFS_WAL_WRITE_LATENCY,
           {"zenfs_wal_write_latency", ZENFS_REPORTER_TYPE_LATENCY}},
          {ZENFS_READ_LATENCY,
           {"zenfs_read_latency", ZENFS_REPORTER_TYPE_LATENCY}},
          {ZENFS_WAL_SYNC_LATENCY,
           {"zenfs_wal_sync_latency", ZENFS_REPORTER_TYPE_LATENCY}},
          {ZENFS_NON_WAL_SYNC_LATENCY,
           {"zenfs_non_wal_sync_latency", ZENFS_REPORTER_TYPE_LATENCY}},
          {ZENFS_ZONE_WRITE_LATENCY,
           {"zenfs_zone_write_latency", ZENFS_REPORTER_TYPE_LATENCY}},
          {ZENFS_ROLL_LATENCY,
           {"zenfs_roll_latency", ZENFS_REPORTER_TYPE_LATENCY}},
          {ZENFS_META_ALLOC_LATENCY,
           {"zenfs_meta_alloc_latency", ZENFS_REPORTER_TYPE_LATENCY}},
          {ZENFS_META_SYNC_LATENCY,
           {"zenfs_meta_sync_latency", ZENFS_REPORTER_TYPE_LATENCY}},
          {ZENFS_WRITE_QPS, {"zenfs_write_qps", ZENFS_REPORTER_TYPE_QPS}},
          {ZENFS_READ_QPS, {"zenfs_read_qps", ZENFS_REPORTER_TYPE_QPS}},
          {ZENFS_SYNC_QPS, {"zenfs_sync_qps", ZENFS_REPORTER_TYPE_QPS}},
          {ZENFS_META_ALLOC_QPS,
           {"zenfs_meta_alloc_qps", ZENFS_REPORTER_TYPE_QPS}},
          {ZENFS_IO_ALLOC_QPS, {"zenfs_io_alloc_qps", ZENFS_REPORTER_TYPE_QPS}},
          {ZENFS_ROLL_QPS, {"zenfs_roll_qps", ZENFS_REPORTER_TYPE_QPS}},
          {ZENFS_WRITE_THROUGHPUT,
           {"zenfs_write_throughput", ZENFS_REPORTER_TYPE_THROUGHPUT}},
          {ZENFS_RESETABLE_ZONES_COUNT,
           {"zenfs_resetable_zones", ZENFS_REPORTER_TYPE_GENERAL}},
          {ZENFS_OPEN_ZONES_COUNT,
           {"zenfs_open_zones", ZENFS_REPORTER_TYPE_GENERAL}},
          {ZENFS_ACTIVE_ZONES_COUNT,
           {"zenfs_active_zones", ZENFS_REPORTER_TYPE_GENERAL}},
      };

  void run();

 public:
  ZenFSPrometheusMetrics();
  ~ZenFSPrometheusMetrics();

 private:
  virtual void AddReporter(uint32_t label, ReporterType type = 0) override;
  virtual void Report(uint32_t label_uint, size_t value,
                      uint32_t type_uint = 0) override;

 public:
  virtual void ReportQPS(uint32_t label, size_t qps) override {
    Report(label, qps, ZENFS_REPORTER_TYPE_QPS);
  }
  virtual void ReportLatency(uint32_t label, size_t latency) override {
    Report(label, latency, ZENFS_REPORTER_TYPE_LATENCY);
  }
  virtual void ReportThroughput(uint32_t label, size_t throughput) override {
    Report(label, throughput, ZENFS_REPORTER_TYPE_THROUGHPUT);
  }
  virtual void ReportGeneral(uint32_t label, size_t value) override {
    Report(label, value, ZENFS_REPORTER_TYPE_GENERAL);
  }

  virtual void ReportSnapshot(const ZenFSSnapshot &snapshot) override {}
};

}  // namespace ROCKSDB_NAMESPACE
