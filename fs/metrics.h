// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

// Metrics Framework Introduction
//
// The metrics framework is used for users to identify ZenFS's performance
// bottomneck, it can collect throuput, qps and latency of each critical
// function call.
//
// For different RocksDB forks, users could custom their own metrics reporter to
// define how they would like to report these collected information.
//
// Steps to add new metrics trace point:
//   1. Add a new trace point label name in `ZenFSMetricsHistograms`.
//   2. Find target function, add these lines for tracing
//       // Latency Trace
//       ZenFSMetricsGuard guard(zoneFile_->GetZBDMetrics(),
//       ZENFS_WAL_WRITE_LATENCY, Env::Default());
//       // Throughput Trace
//       zoneFile_->GetZBDMetrics()->ReportThroughput(ZENFS_WRITE_THROUGHPUT,
//       data.size());
//       // QPS Trace
//       zoneFile_->GetZBDMetrics()->ReportQPS(ZENFS_WRITE_QPS, 1);
//    3. Implement a `ZenFSMetrics` to define how you would like to report your
//    data (Refer to file  `metrics_sample.h`)
//    4. Define your customized label name when implement ZenFSMetrics
//    5. Init your metrics and pass it into `NewZenFS()` function (default is
//    `NoZenFSMetrics`)

#pragma once
#include "rocksdb/env.h"
namespace ROCKSDB_NAMESPACE {

class ZenFSMetricsGuard;
class ZenFSSnapshot;
class ZenFSSnapshotOptions;

// Types of Reporter that may be used for statistics.
enum ZenFSMetricsReporterType : uint32_t {
  ZENFS_REPORTER_TYPE_WITHOUT_CHECK = 0,
  ZENFS_REPORTER_TYPE_GENERAL,
  ZENFS_REPORTER_TYPE_LATENCY,
  ZENFS_REPORTER_TYPE_QPS,
  ZENFS_REPORTER_TYPE_THROUGHPUT,
};

// Names of Reporter that may be used for statistics.
enum ZenFSMetricsHistograms : uint32_t {
  ZENFS_HISTOGRAM_ENUM_MIN,

  ZENFS_READ_LATENCY,
  ZENFS_READ_QPS,

  ZENFS_WRITE_LATENCY,
  ZENFS_WAL_WRITE_LATENCY,
  ZENFS_NON_WAL_WRITE_LATENCY,
  ZENFS_WRITE_QPS,
  ZENFS_WRITE_THROUGHPUT,

  ZENFS_SYNC_LATENCY,
  ZENFS_WAL_SYNC_LATENCY,
  ZENFS_NON_WAL_SYNC_LATENCY,
  ZENFS_SYNC_QPS,

  ZENFS_IO_ALLOC_LATENCY,
  ZENFS_WAL_IO_ALLOC_LATENCY,
  ZENFS_NON_WAL_IO_ALLOC_LATENCY,
  ZENFS_IO_ALLOC_QPS,

  ZENFS_META_ALLOC_LATENCY,
  ZENFS_META_ALLOC_QPS,

  ZENFS_META_SYNC_LATENCY,

  ZENFS_ROLL_LATENCY,
  ZENFS_ROLL_QPS,
  ZENFS_ROLL_THROUGHPUT,

  ZENFS_ACTIVE_ZONES_COUNT,
  ZENFS_OPEN_ZONES_COUNT,

  ZENFS_FREE_SPACE_SIZE,
  ZENFS_USED_SPACE_SIZE,
  ZENFS_RECLAIMABLE_SPACE_SIZE,

  ZENFS_RESETABLE_ZONES_COUNT,

  ZENFS_HISTOGRAM_ENUM_MAX,

  ZENFS_ZONE_WRITE_THROUGHPUT,
  ZENFS_ZONE_WRITE_LATENCY,

  ZENFS_L0_IO_ALLOC_LATENCY,
};

struct ZenFSMetrics {
 public:
  typedef uint32_t Label;
  typedef uint32_t ReporterType;
  // We give an enum to identify the reporters and an enum to identify the
  // reporter types: ZenFSMetricsHistograms and ZenFSMetricsReporterType,
  // respectively, at the end of the code.
 public:
  ZenFSMetrics() {}
  virtual ~ZenFSMetrics() {}

 public:
  // Add a reporter named label.
  // You can give a type for type-checking.
  virtual void AddReporter(Label label, ReporterType type = 0) = 0;
  // Report a value for the reporter named label.
  // You can give a type for type-checking.
  virtual void Report(Label label, size_t value,
                      ReporterType type_check = 0) = 0;
  virtual void ReportSnapshot(const ZenFSSnapshot& snapshot) = 0;

 public:
  // Syntactic sugars for type-checking.
  // Overwrite them if you think type-checking is necessary.
  virtual void ReportQPS(Label label, size_t qps) { Report(label, qps, 0); }
  virtual void ReportThroughput(Label label, size_t throughput) {
    Report(label, throughput, 0);
  }
  virtual void ReportLatency(Label label, size_t latency) {
    Report(label, latency, 0);
  }
  virtual void ReportGeneral(Label label, size_t data) {
    Report(label, data, 0);
  }

  // and more
};

struct NoZenFSMetrics : public ZenFSMetrics {
  NoZenFSMetrics() : ZenFSMetrics() {}
  virtual ~NoZenFSMetrics() {}

 public:
  virtual void AddReporter(uint32_t /*label*/, uint32_t /*type*/) override {}
  virtual void Report(uint32_t /*label*/, size_t /*value*/,
                      uint32_t /*type_check*/) override {}
  virtual void ReportSnapshot(const ZenFSSnapshot& /*snapshot*/) override {}
};

// The implementation of this class will start timing when initialized,
// stop timing when it is destructured,
// and report the difference in time to the target label via
// metrics->ReportLatency(). By default, the method to collect the time will be
// to call env->NowMicros().
struct ZenFSMetricsLatencyGuard {
  std::shared_ptr<ZenFSMetrics> metrics_;
  uint32_t label_;
  Env* env_;
  uint64_t begin_time_micro_;

  ZenFSMetricsLatencyGuard(std::shared_ptr<ZenFSMetrics> metrics,
                           uint32_t label, Env* env)
      : metrics_(metrics),
        label_(label),
        env_(env),
        begin_time_micro_(GetTime()) {}

  virtual ~ZenFSMetricsLatencyGuard() {
    uint64_t end_time_micro_ = GetTime();
    assert(end_time_micro_ >= begin_time_micro_);
    metrics_->ReportLatency(label_,
                            Report(end_time_micro_ - begin_time_micro_));
  }
  // overwrite this function if you wish to capture time by other methods.
  virtual uint64_t GetTime() { return env_->NowMicros(); }
  // overwrite this function if you do not intend to report delays measured in
  // microseconds.
  virtual uint64_t Report(uint64_t time) { return time; }
};

#define ZENFS_LABEL(label, type) ZENFS_##label##_##type
#define ZENFS_LABEL_DETAILED(label, sub_label, type) \
  ZENFS_##sub_label##_##label##_##type
// eg : ZENFS_LABEL(WRITE, WAL, THROUGHPUT) => ZENFS_WAL_WRITE_THROUGHPUT

}  // namespace ROCKSDB_NAMESPACE
