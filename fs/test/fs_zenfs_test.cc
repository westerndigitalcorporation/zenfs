

#include "../fs_zenfs.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

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

TEST_F(FsZenFSTest, SimpleAuxDirRename) {
  IOStatus status;

  ASSERT_TRUE(1);

  IOOptions options;
  IODebugContext dbg;
  std::string initial_dir_name = "test";
  std::string renamed_dir_name = "renamed";
  status = fs_->CreateDir(initial_dir_name, options, nullptr);
  ASSERT_TRUE(status.ok()) << "Failed to create directory '" << initial_dir_name
                           << "'";

  status = fs_->RenameFile(initial_dir_name, renamed_dir_name, options, &dbg);
  ASSERT_TRUE(status.ok()) << "Directory '" << initial_dir_name
                           << "' could not be renamed to '" << renamed_dir_name
                           << "'";

  status = fs_->FileExists(initial_dir_name, options, &dbg);
  ASSERT_TRUE(!status.ok()) << "Directory '" << initial_dir_name
                            << "' still exists after renaming it";

  status = fs_->FileExists(renamed_dir_name, options, &dbg);
  ASSERT_TRUE(status.ok()) << "Directory '" << renamed_dir_name
                           << "' does not exists";
};

TEST_F(FsZenFSTest, SimpleZenfsFileRename) {
  IOStatus status;

  ASSERT_TRUE(1);

  IOOptions options;
  IODebugContext dbg;
  FileOptions foptions;
  std::unique_ptr<FSWritableFile> file;
  std::string initial_file_name = "test";
  std::string renamed_file_name = "renamed";

  status = fs_->NewWritableFile(initial_file_name, foptions, &file, &dbg);
  ASSERT_TRUE(status.ok()) << "Failed to create NewWritableFile '"
                           << initial_file_name << "'";

  status = fs_->RenameFile(initial_file_name, renamed_file_name, options, &dbg);
  ASSERT_TRUE(status.ok()) << "File '" << initial_file_name
                           << "' could not be renamed to '" << renamed_file_name
                           << "'";

  status = fs_->FileExists(initial_file_name, options, &dbg);
  ASSERT_TRUE(!status.ok())
      << "File '" << initial_file_name << "' still exists after renaming it";

  status = fs_->FileExists(renamed_file_name, options, &dbg);
  ASSERT_TRUE(status.ok()) << "File '" << renamed_file_name
                           << "' does not exists";
};

TEST_F(FsZenFSTest, DirectoriesWithGrandchildrenRename) {
  // Directory structure:
  // aux_dir/
  // └──- test/ (->renamed)
  //      ├─- childdir/
  //      |   └── grandchildtestfile
  //      └── childtestfile
  IOStatus status;

  ASSERT_TRUE(1);

  IOOptions options;
  IODebugContext dbg;
  FileOptions foptions;
  std::unique_ptr<FSWritableFile> child_file;
  std::unique_ptr<FSWritableFile> grandchild_file;
  std::string initial_dir_name = "test";
  std::string renamed_dir_name = "renamed";
  std::string child_file_name = "childtestfile";
  std::string child_dir_name = "childdir";
  std::string grandchild_file_name = "grandchildtestfile";

  status = fs_->CreateDir(initial_dir_name, options, nullptr);
  ASSERT_TRUE(status.ok()) << "Failed to create directory '" << initial_dir_name
                           << "'";

  std::string child_file_path =
      std::filesystem::path(initial_dir_name) / child_file_name;
  status = fs_->NewWritableFile(child_file_path, foptions, &child_file, &dbg);
  ASSERT_TRUE(status.ok()) << "Failed to create NewWritableFile '"
                           << child_file_path << "'";

  std::string child_dir_path =
      std::filesystem::path(initial_dir_name) / child_dir_name;
  status = fs_->CreateDir(child_dir_path, options, nullptr);
  ASSERT_TRUE(status.ok()) << "Failed to create directory '" << child_dir_path
                           << "'";

  std::string grandchild_file_path =
      std::filesystem::path(child_dir_path) / grandchild_file_name;
  status = fs_->NewWritableFile(grandchild_file_path, foptions,
                                &grandchild_file, &dbg);
  ASSERT_TRUE(status.ok()) << "Failed to create NewWritableFile '"
                           << grandchild_file_path << "'";

  status = fs_->RenameFile(initial_dir_name, renamed_dir_name, options, &dbg);
  ASSERT_TRUE(status.ok()) << "Directory '" << initial_dir_name
                           << "' could not be renamed to '" << renamed_dir_name
                           << "'";

  std::string renamed_child_file_path =
      std::filesystem::path(renamed_dir_name) / child_file_name;
  status = fs_->FileExists(renamed_child_file_path, options, &dbg);
  ASSERT_TRUE(status.ok()) << "File '" << renamed_child_file_path
                           << "' does not exists";

  status = fs_->FileExists(child_file_path, options, &dbg);
  ASSERT_TRUE(!status.ok())
      << "File '" << child_file_path << "' still exists after renaming it";

  status = fs_->FileExists(renamed_dir_name, options, &dbg);
  ASSERT_TRUE(status.ok()) << "Directory '" << renamed_dir_name
                           << "' does not exists";

  status = fs_->FileExists(initial_dir_name, options, &dbg);
  ASSERT_TRUE(!status.ok()) << "Directory '" << initial_dir_name
                            << "' still exists after renaming it";

  std::string renamed_child_dir_path =
      std::filesystem::path(renamed_dir_name) / child_dir_name;
  status = fs_->FileExists(renamed_child_dir_path, options, &dbg);
  ASSERT_TRUE(status.ok()) << "Directory '" << renamed_child_dir_path
                           << "' does not exists";

  status = fs_->FileExists(child_dir_path, options, &dbg);
  ASSERT_TRUE(!status.ok())
      << "Directory '" << child_dir_path << "' still exists after renaming it";

  std::string renamed_grandchild_file_path =
      std::filesystem::path(renamed_child_dir_path) / grandchild_file_name;
  status = fs_->FileExists(renamed_grandchild_file_path, options, &dbg);
  ASSERT_TRUE(status.ok()) << "File '" << renamed_grandchild_file_path
                           << "' does not exists";

  status = fs_->FileExists(grandchild_file_path, options, &dbg);
  ASSERT_TRUE(!status.ok())
      << "File '" << grandchild_file_path << "' still exists after renaming it";
};

TEST_F(FsZenFSTest, SimilarDirectoriesRename) {
  // The directory initial_dir_name is renamed.
  // initial_dir_name is a substring of unrelated_dir_name,
  // which should not get renamed including its childen.
  //
  // Directory structure:
  // aux_dir/
  // ├───- test/ (->renamed)
  // |     └── testfile
  // └──-- test2/
  //       └── unrelatedfile
  IOStatus status;

  ASSERT_TRUE(1);

  IOOptions options;
  IODebugContext dbg;
  FileOptions foptions;
  std::unique_ptr<FSWritableFile> nested_file;
  std::unique_ptr<FSWritableFile> unrelated_file;
  std::string initial_dir_name = "test";
  std::string renamed_dir_name = "renamed";
  std::string nested_file_name = "testfile";
  std::string unrelated_dir_name = "test2";
  std::string unrelated_file_name = "unrelatedfile";

  status = fs_->CreateDir(initial_dir_name, options, nullptr);
  ASSERT_TRUE(status.ok()) << "Failed to create directory '" << initial_dir_name
                           << "'";

  status = fs_->CreateDir(unrelated_dir_name, options, nullptr);
  ASSERT_TRUE(status.ok()) << "Failed to create directory '"
                           << unrelated_dir_name << "'";

  std::string nested_file_path =
      std::filesystem::path(initial_dir_name) / nested_file_name;
  status = fs_->NewWritableFile(nested_file_path, foptions, &nested_file, &dbg);
  ASSERT_TRUE(status.ok()) << "Failed to create NewWritableFile '"
                           << nested_file_path << "'";

  std::string unrelated_file_path =
      std::filesystem::path(unrelated_dir_name) / unrelated_file_name;
  status = fs_->NewWritableFile(unrelated_file_path, foptions, &unrelated_file,
                                &dbg);
  ASSERT_TRUE(status.ok()) << "Failed to create NewWritableFile '"
                           << unrelated_file_path << "'";

  status = fs_->RenameFile(initial_dir_name, renamed_dir_name, options, &dbg);
  ASSERT_TRUE(status.ok()) << "Directory '" << initial_dir_name
                           << "' could not be renamed to '" << renamed_dir_name
                           << "'";

  std::string renamed_nested_file_path =
      std::filesystem::path(renamed_dir_name) / nested_file_name;
  status = fs_->FileExists(renamed_nested_file_path, options, &dbg);
  ASSERT_TRUE(status.ok()) << "File '" << renamed_nested_file_path
                           << "' does not exists";

  status = fs_->FileExists(nested_file_path, options, &dbg);
  ASSERT_TRUE(!status.ok())
      << "File '" << nested_file_path << "' still exists after renaming it";

  status = fs_->FileExists(renamed_dir_name, options, &dbg);
  ASSERT_TRUE(status.ok()) << "Directory '" << renamed_dir_name
                           << "' does not exists";

  status = fs_->FileExists(initial_dir_name, options, &dbg);
  ASSERT_TRUE(!status.ok()) << "Directory '" << initial_dir_name
                            << "' still exists after renaming it";

  status = fs_->FileExists(unrelated_dir_name, options, &dbg);
  ASSERT_TRUE(status.ok()) << "Directory '" << unrelated_dir_name
                           << "' does not exists";

  status = fs_->FileExists(unrelated_file_path, options, &dbg);
  ASSERT_TRUE(status.ok()) << "Directory '" << unrelated_file_path
                           << "' does not exists";
};

}  // namespace ROCKSDB_NAMESPACE

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
