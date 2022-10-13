/*
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <android-base/logging.h>

#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/commands/cvd/parser/instance/cf_vm_configs.h"
namespace cuttlefish {

static std::map<std::string, Json::ValueType> kVmKeyMap = {
    {"cpus", Json::ValueType::intValue},
    {"memory_mb", Json::ValueType::intValue}};

bool ValidateVmConfigs(const Json::Value& root) {
  if (!ValidateTypo(root, kVmKeyMap)) {
    LOG(INFO) << "ValidateVmConfigs ValidateTypo fail";
    return false;
  }
  return true;
}

void InitVmConfigs(Json::Value& instances) {
  InitIntConfig(instances, "vm", "cpus", CF_DEFAULTS_CPUS);
  InitIntConfig(instances, "vm", "memory_mb", CF_DEFAULTS_MEMORY_MB);
}

void GenerateVmConfigs(const Json::Value& instances,
                       std::vector<std::string>& result) {
  result.emplace_back(GenerateGflag(instances, "cpus", "vm", "cpus"));
  result.emplace_back(GenerateGflag(instances, "memory_mb", "vm", "memory_mb"));
}

}  // namespace cuttlefish