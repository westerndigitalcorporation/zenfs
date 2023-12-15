#include <assert.h>

#include <iostream>

#include "rocksdb/db.h"
#include "rocksdb/env.h"
#include "rocksdb/options.h"
#include "rocksdb/utilities/options_util.h"

int main(int argc, char* argv[]) {
  if (argc != 2) {
    printf("Usage:\n\t%s <device-name>\nE.g.:\n\t%s nvme0n1\n", argv[0],
           argv[0]);
    return 1;
  }

  int ret = 0;
  std::string test_value;
  std::string device_name = argv[1];
  rocksdb::DB* db;
  static std::shared_ptr<rocksdb::Env> fs_env_guard;
  static rocksdb::Env* fs_env = nullptr;
  rocksdb::Options options;
  options.create_if_missing = true;
  options.use_direct_io_for_flush_and_compaction = true;
  rocksdb::ConfigOptions config_options(options);

  rocksdb::Env::CreateFromUri(config_options, "", "zenfs://dev:" + device_name,
                              &fs_env, &fs_env_guard);
  options.env = fs_env;

  // Open database
  rocksdb::Status status = rocksdb::DB::Open(options, "rocksdbtest", &db);
  if (!status.ok()) {
    std::cerr << "Could not open the database: " << status.ToString()
              << std::endl;
    ret = 1;
    goto out;
  }

  // Write
  status =
      db->Put(rocksdb::WriteOptions(), "zenfs_test_var", "zenfs_test_value");
  if (!status.ok()) {
    std::cerr << "Could not write test value to the database: "
              << status.ToString() << std::endl;
    ret = 1;
  }

  // Read
  status = db->Get(rocksdb::ReadOptions(), "zenfs_test_var", &test_value);
  if (!status.ok()) {
    std::cerr << "Could not read test value to the database: "
              << status.ToString() << std::endl;
    ret = 1;
    goto out;
  } else {
    std::cout << "Value was successfully written and read: " << test_value
              << std::endl;
    ret = 0;
  }

out:
  if (db) delete db;
  return ret;
}
