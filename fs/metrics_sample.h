// SPDX-License-Identifier: Apache License 2.0 OR GPL-2.0

#include <iostream>
#include <unordered_map>

#include "metrics.h"
#include "port/port.h"
#include "snapshot.h"
#include "util/mutexlock.h"

namespace ROCKSDB_NAMESPACE {

const std::unordered_map<uint32_t, std::pair<std::string, uint32_t>>
    ZenFSHistogramsNameMap = {
        {ZENFS_WRITE_LATENCY,
         {"zenfs_write_latency", ZENFS_REPORTER_TYPE_LATENCY}},
        {ZENFS_WRITE_QPS, {"zenfs_write_qps", ZENFS_REPORTER_TYPE_QPS}},
        {ZENFS_RESETABLE_ZONES_COUNT,
         {"zenfs_resetable_zones", ZENFS_REPORTER_TYPE_GENERAL}}};

struct ReporterSample {
 public:
  typedef uint64_t TypeTime;
  typedef uint64_t TypeValue;
  typedef std::pair<TypeTime, TypeValue> TypeRecord;

 private:
  port::Mutex mu_;
  ZenFSMetricsReporterType type_;
  std::vector<TypeRecord> hist_;

  static const TypeTime MinReportInterval =
      30 * 1000000;  // 30 seconds for all reporters by default.
  bool ReadyToReport(uint64_t time) const {
    // AssertHeld(&mu);
    if (hist_.size() == 0) return 1;
    TypeTime last_report_time = hist_.rbegin()->first;
    return time > last_report_time + MinReportInterval;
  }

 public:
  ReporterSample(ZenFSMetricsReporterType type) : mu_(), type_(type), hist_() {}
  void Record(const TypeTime& time, TypeValue value) {
    MutexLock guard(&mu_);
    if (ReadyToReport(time)) hist_.push_back(TypeRecord(time, value));
  }
  ZenFSMetricsReporterType Type() const { return type_; }
  void GetHistSnapshot(std::vector<TypeRecord>& hist) {
    MutexLock guard(&mu_);
    hist = hist_;
  }
};

struct ZenFSMetricsSample : public ZenFSMetrics {
 public:
  typedef uint64_t TypeMicroSec;
  typedef ReporterSample TypeReporter;

 private:
  Env* env_;
  std::unordered_map<ZenFSMetricsHistograms, TypeReporter> reporter_map_;

 public:
  ZenFSMetricsSample(Env* env) : env_(env), reporter_map_() {
    for (auto& label_with_type : ZenFSHistogramsNameMap)
      AddReporter(static_cast<uint32_t>(label_with_type.first),
                  static_cast<uint32_t>(label_with_type.second.second));
  }
  ~ZenFSMetricsSample() {}

  virtual void AddReporter(uint32_t label_uint,
                           uint32_t type_uint = 0) override {
    auto label = static_cast<ZenFSMetricsHistograms>(label_uint);
    assert(ZenFSHistogramsNameMap.find(label) != ZenFSHistogramsNameMap.end());

    auto pair = ZenFSHistogramsNameMap.find(label)->second;
    auto type = pair.second;

    if (type_uint != 0) {
      auto type_check = static_cast<ZenFSMetricsReporterType>(type_uint);
      assert(type_check == type);
      (void)type_check;
    }

    switch (type) {
      case ZENFS_REPORTER_TYPE_GENERAL:
      case ZENFS_REPORTER_TYPE_LATENCY:
      case ZENFS_REPORTER_TYPE_QPS:
      case ZENFS_REPORTER_TYPE_THROUGHPUT:
      case ZENFS_REPORTER_TYPE_WITHOUT_CHECK: {
        reporter_map_.emplace(label, type);
      } break;
    }
  }
  virtual void Report(uint32_t label_uint, size_t value,
                      uint32_t type_uint = 0) override {
    auto label = static_cast<ZenFSMetricsHistograms>(label_uint);
    assert(ZenFSHistogramsNameMap.find(label) != ZenFSHistogramsNameMap.end());
    auto p = reporter_map_.find(static_cast<ZenFSMetricsHistograms>(label));
    assert(p != reporter_map_.end());
    TypeReporter& reporter = p->second;
    auto type = reporter.Type();

    if (type_uint != 0) {
      auto type_check = static_cast<ZenFSMetricsReporterType>(type_uint);
      assert(type_check == type);
      (void)type_check;
    }

    switch (type) {
      case ZENFS_REPORTER_TYPE_GENERAL:
      case ZENFS_REPORTER_TYPE_LATENCY:
      case ZENFS_REPORTER_TYPE_QPS:
      case ZENFS_REPORTER_TYPE_THROUGHPUT:
      case ZENFS_REPORTER_TYPE_WITHOUT_CHECK: {
        reporter.Record(GetTime(), value);
      } break;
    }
  }

  virtual void ReportSnapshot(const ZenFSSnapshot& snapshot) {
    uint64_t free_space_gb = snapshot.zbd_.free_space >> 30;
    ReportGeneral(ZENFS_LABEL(FREE_SPACE, SIZE), free_space_gb);
    // Report anything you care about.
  }

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

 public:
  virtual void DebugPrint(std::ostream& os) {
    os << "[Text histogram from ZenFSMetricsSample: ]{" << std::endl;
    for (auto& label_with_rep : reporter_map_) {
      auto label = label_with_rep.first;
      auto& reporter = label_with_rep.second;
      auto pair = ZenFSHistogramsNameMap.find(label)->second;
      const std::string& name = pair.first;
      os << "  " << name << ":[";

      std::vector<std::pair<uint64_t, uint64_t>> hist;
      reporter.GetHistSnapshot(hist);
      for (auto& time_with_value : hist) {
        auto time = time_with_value.first;
        auto value = time_with_value.second;
        os << "(" << time << "," << value << "),";
      }

      os << "]" << std::endl;
    }
    os << "}[End Histogram.]" << std::endl;
  }

 private:
  uint64_t GetTime() { return env_->NowMicros(); }
};

}  // namespace ROCKSDB_NAMESPACE
