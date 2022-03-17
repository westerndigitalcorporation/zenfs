

#include "../fs_zenfs.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <memory>

#include "../util.h"
#include "rocksdb/convenience.h"
#include "rocksdb/env.h"

namespace ROCKSDB_NAMESPACE {

class FsZenFSTest : public testing::Test {
 private:
  std::filesystem::path aux_path;

  void uniqueue_path(std::filesystem::path& out_path) {
    char* name = std::tmpnam(nullptr);
    ASSERT_TRUE(name != nullptr);

    out_path = std::filesystem::path(name);
  }

 public:
  std::shared_ptr<FileSystem> fs_;

  FsZenFSTest() {}

 protected:
  void CreateFilesystem(std::string const& device) {
    Status status;
    namespace fs = std::filesystem;
    using fs::path;

    uniqueue_path(aux_path);
    status = zenfs_mkfs(device, aux_path, 0, true);
    ASSERT_TRUE(status.ok())
        << "Failed to create ZenFS filesystem on device : " << device
        << " with aux path: " << aux_path << " error: " << status.ToString();
  }

  void SetUp() override {
    Status status;
    std::string fs_uri;
    char* env_device;
    ConfigOptions config_options;

    env_device = std::getenv("DEVICE");
    ASSERT_TRUE(env_device != nullptr) << "DEVICE environment variable not set";

    CreateFilesystem(env_device);

    config_options.ignore_unsupported_options = false;
    fs_uri = "zenfs://dev:" + std::string(env_device);
    status = FileSystem::CreateFromString(config_options, fs_uri, &fs_);
    ASSERT_TRUE(status.ok()) << "Failed to create ZenFS instance";
  }

  void TearDown() override { std::filesystem::remove_all(aux_path); }
};

TEST_F(FsZenFSTest, Example) {
  IOStatus status;

  ASSERT_TRUE(1);

  IOOptions options;
  status = fs_->CreateDir("test", options, nullptr);
  ASSERT_TRUE(status.ok()) << "Failed to create directory 'test'";

  status = fs_->CreateDir("test", options, nullptr);
  ASSERT_TRUE(!status.ok()) << "Directory 'test' could be created twice";
};
}  // namespace ROCKSDB_NAMESPACE

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
