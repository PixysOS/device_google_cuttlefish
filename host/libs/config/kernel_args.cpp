/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "host/libs/config/kernel_args.h"

#include <string>
#include <vector>

#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/vm_manager/vm_manager.h"

template<typename T>
static void AppendVector(std::vector<T>* destination, const std::vector<T>& source) {
  destination->insert(destination->end(), source.begin(), source.end());
}

template<typename S, typename T>
static std::string concat(const S& s, const T& t) {
  std::ostringstream os;
  os << s << t;
  return os.str();
}

std::vector<std::string> KernelCommandLineFromConfig(const vsoc::CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  std::vector<std::string> kernel_cmdline;

  AppendVector(&kernel_cmdline, config.boot_image_kernel_cmdline());
  AppendVector(&kernel_cmdline,
               vm_manager::VmManager::ConfigureGpuMode(config.vm_manager(), config.gpu_mode()));
  AppendVector(&kernel_cmdline, vm_manager::VmManager::ConfigureBootDevices(config.vm_manager()));

  kernel_cmdline.push_back(concat("androidboot.serialno=", instance.serial_number()));
  kernel_cmdline.push_back(concat("androidboot.lcd_density=", config.dpi()));
  kernel_cmdline.push_back(concat(
      "androidboot.setupwizard_mode=", config.setupwizard_mode()));
  if (!config.use_bootloader()) {
    std::string slot_suffix;
    if (config.boot_slot().empty()) {
      slot_suffix = "_a";
    } else {
      slot_suffix = "_" + config.boot_slot();
    }
    kernel_cmdline.push_back(concat("androidboot.slot_suffix=", slot_suffix));
  }
  kernel_cmdline.push_back(concat("loop.max_part=", config.loop_max_part()));
  if (config.guest_enforce_security()) {
    kernel_cmdline.push_back("enforcing=1");
  } else {
    kernel_cmdline.push_back("enforcing=0");
    kernel_cmdline.push_back("androidboot.selinux=permissive");
  }
  if (config.guest_audit_security()) {
    kernel_cmdline.push_back("audit=1");
  } else {
    kernel_cmdline.push_back("audit=0");
  }
  if (config.guest_force_normal_boot()) {
    kernel_cmdline.push_back("androidboot.force_normal_boot=1");
  }

  if (config.enable_tombstone_receiver() && instance.tombstone_receiver_port()) {
    kernel_cmdline.push_back("androidboot.tombstone_transmit=1");
    kernel_cmdline.push_back(concat("androidboot.vsock_tombstone_port=", instance.tombstone_receiver_port()));
  } else {
    kernel_cmdline.push_back("androidboot.tombstone_transmit=0");
  }

  if (config.logcat_mode() == cvd::kLogcatVsockMode && instance.logcat_port()) {
    kernel_cmdline.push_back(concat("androidboot.vsock_logcat_port=", instance.logcat_port()));
  }

  if (instance.config_server_port()) {
    kernel_cmdline.push_back(concat("androidboot.cuttlefish_config_server_port=", instance.config_server_port()));
  }

  if (instance.tpm_port()) {
    kernel_cmdline.push_back(concat("androidboot.tpm_vsock_port=", instance.tpm_port()));
  }

  if (instance.keyboard_server_port()) {
    kernel_cmdline.push_back(concat("androidboot.vsock_keyboard_port=", instance.keyboard_server_port()));
  }

  if (instance.touch_server_port()) {
    kernel_cmdline.push_back(concat("androidboot.vsock_touch_port=", instance.touch_server_port()));
  }

  if (instance.frames_server_port()) {
    kernel_cmdline.push_back(concat("androidboot.vsock_frames_port=", instance.frames_server_port()));
  }

  AppendVector(&kernel_cmdline, config.extra_kernel_cmdline());

  return kernel_cmdline;
}
