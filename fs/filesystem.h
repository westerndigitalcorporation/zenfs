// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#pragma once

#if __cplusplus < 201703L
#include "filesystem_utility.h"
namespace fs = filesystem_utility;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif
