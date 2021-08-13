#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <gflags/gflags.h>
#include <libzbd/zbd.h>
#include <linux/blkzoned.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using GFLAGS_NAMESPACE::ParseCommandLineFlags;
using GFLAGS_NAMESPACE::RegisterFlagValidator;
using GFLAGS_NAMESPACE::SetUsageMessage;

DEFINE_string(zbd, "", "Path to a zoned block device.");
DEFINE_uint64(fill_bytes, 65536 * 10000,
              "Bytes to fill before finishing a zone.");
DEFINE_uint64(epoch, 10000, "Rewrite zone for N times");

std::string ErrorToString(int err) {
  char* err_str = strerror(err);
  if (err_str != nullptr) return std::string(err_str);
  return "";
}

double percentile(const std::vector<double>& latencies, double percentile) {
  int index = (int)ceil(percentile / 100.0 * latencies.size());
  return latencies[index - 1];
}

void print_percentile(std::vector<double>& latencies) {
  std::sort(std::begin(latencies), std::end(latencies));
  std::cout << "P50: " << percentile(latencies, 0.5) << std::endl;
  std::cout << "P95: " << percentile(latencies, 0.95) << std::endl;
  std::cout << "P99: " << percentile(latencies, 0.99) << std::endl;
  std::cout << "P999: " << percentile(latencies, 0.999) << std::endl;
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_zbd.empty()) {
    fprintf(stderr, "You need to specify a zoned block device using --zbd\n");
    return 1;
  }

  zbd_info info;
  int zbd_fileno =
      zbd_open(FLAGS_zbd.c_str(), O_WRONLY | O_DIRECT | O_EXCL, &info);

  if (zbd_fileno < 0) {
    std::cerr << "Failed to open zbd device: " << ErrorToString(errno)
              << std::endl;
    return 1;
  }

  struct zbd_zone* zone_rep;
  unsigned int reported_zones;

  int ret = zbd_list_zones(zbd_fileno, 0, info.nr_zones * info.zone_size,
                           ZBD_RO_ALL, &zone_rep, &reported_zones);

  if (ret || reported_zones != info.nr_zones) {
    std::cerr << "Failed to list zones: " << ret << std::endl;
    return 1;
  }

  std::vector<zbd_zone*> zones;

  for (auto i = 0; i < reported_zones; i++) {
    struct zbd_zone* z = &zone_rep[i];
    /* Only use sequential write required zones */
    if (zbd_zone_type(z) == ZBD_ZONE_TYPE_SWR) {
      if (!zbd_zone_offline(z)) {
        zones.push_back(z);
        ret = zbd_reset_zones(zbd_fileno, z->start, z->capacity);
        if (ret) {
          std::cerr << "Failed to reset zone " << ret << std::endl;
          return 1;
        }
      }
    }
  }

  std::default_random_engine generator;
  std::uniform_int_distribution<int> distribution(0, zones.size() - 1);

  std::cout << zones.size() << " zones available" << std::endl;

  std::vector<double> write_zones_histogram;
  std::vector<double> finish_zones_histogram;
  std::vector<double> reset_zones_histogram;

  // 16KB buffer
  char* buf;
  const unsigned long write_unit = 65536;
  ret = posix_memalign((void**)&buf, 4096, write_unit);
  if (ret) {
    std::cerr << "Failed to allocate buffer " << ret << std::endl;
  }

  for (auto t = 0; t < FLAGS_epoch; t++) {
    int selected_zone = distribution(generator);
    zbd_zone* z = zones[selected_zone];
    int ret;
    std::chrono::high_resolution_clock::time_point start;
    start = std::chrono::high_resolution_clock::now();
    ret = zbd_open_zones(zbd_fileno, z->start, z->capacity);
    if (ret) {
      std::cerr << "Failed to open zone " << ret << std::endl;
      return 1;
    }

    for (auto pos = 0; pos < FLAGS_fill_bytes; pos += write_unit) {
      auto write_size = pos + write_unit > FLAGS_fill_bytes
                            ? FLAGS_fill_bytes - pos
                            : write_unit;
      auto size = pwrite(zbd_fileno, buf, write_size, z->start + pos);
      if (size != write_size) {
        std::cerr << "Failed to pwrite zone: written_size=" << size
                  << " expected_size=" << write_size << " pos=" << pos
                  << " zone=" << selected_zone << std::endl;
        return 1;
      }
    }
    write_zones_histogram.push_back(
        (std::chrono::high_resolution_clock::now() - start).count());

    start = std::chrono::high_resolution_clock::now();
    ret = zbd_finish_zones(zbd_fileno, z->start, z->capacity);
    if (ret) {
      std::cerr << "Failed to finish zone " << ret << std::endl;
      return 1;
    }
    finish_zones_histogram.push_back(
        (std::chrono::high_resolution_clock::now() - start).count());

    start = std::chrono::high_resolution_clock::now();
    ret = zbd_reset_zones(zbd_fileno, z->start, z->capacity);
    if (ret) {
      std::cerr << "Failed to reset zone " << ret << std::endl;
      return 1;
    }
    reset_zones_histogram.push_back(
        (std::chrono::high_resolution_clock::now() - start).count());

    std::cout << "--- WRITE ZONES ---" << std::endl;
    print_percentile(write_zones_histogram);

    std::cout << "--- FINISH ZONES ---" << std::endl;
    print_percentile(finish_zones_histogram);

    std::cout << "--- RESET ZONES ---" << std::endl;
    print_percentile(reset_zones_histogram);
  }
  return 0;
}
