//
// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <iostream>

#include <android-base/strings.h>
#include <android-base/logging.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/tee_logging.h"
#include "host/commands/assemble_cvd/clean.h"
#include "host/commands/assemble_cvd/disk_flags.h"
#include "host/commands/assemble_cvd/flag_feature.h"
#include "host/commands/assemble_cvd/flags.h"
#include "host/libs/config/adb/adb.h"
#include "host/libs/config/config_flag.h"
#include "host/libs/config/custom_actions.h"
#include "host/libs/config/fetcher_config.h"

using cuttlefish::StringFromEnv;

DEFINE_string(assembly_dir, StringFromEnv("HOME", ".") + "/cuttlefish_assembly",
              "A directory to put generated files common between instances");
DEFINE_string(instance_dir, StringFromEnv("HOME", ".") + "/cuttlefish_runtime",
              "A directory to put all instance specific files");
DEFINE_bool(resume, true, "Resume using the disk from the last session, if "
                          "possible. i.e., if --noresume is passed, the disk "
                          "will be reset to the state it was initially launched "
                          "in. This flag is ignored if the underlying partition "
                          "images have been updated since the first launch.");
DEFINE_int32(modem_simulator_count, 1,
             "Modem simulator count corresponding to maximum sim number");

namespace cuttlefish {
namespace {

std::string kFetcherConfigFile = "fetcher_config.json";

FetcherConfig FindFetcherConfig(const std::vector<std::string>& files) {
  FetcherConfig fetcher_config;
  for (const auto& file : files) {
    if (android::base::EndsWith(file, kFetcherConfigFile)) {
      if (fetcher_config.LoadFromFile(file)) {
        return fetcher_config;
      }
      LOG(ERROR) << "Could not load fetcher config file.";
    }
  }
  return fetcher_config;
}

std::string GetLegacyConfigFilePath(const CuttlefishConfig& config) {
  return config.ForDefaultInstance().PerInstancePath("cuttlefish_config.json");
}

bool SaveConfig(const CuttlefishConfig& tmp_config_obj) {
  auto config_file = GetConfigFilePath(tmp_config_obj);
  auto config_link = GetGlobalConfigFileLink();
  // Save the config object before starting any host process
  if (!tmp_config_obj.SaveToFile(config_file)) {
    LOG(ERROR) << "Unable to save config object";
    return false;
  }
  auto legacy_config_file = GetLegacyConfigFilePath(tmp_config_obj);
  if (!tmp_config_obj.SaveToFile(legacy_config_file)) {
    LOG(ERROR) << "Unable to save legacy config object";
    return false;
  }
  setenv(kCuttlefishConfigEnvVarName, config_file.c_str(), true);
  if (symlink(config_file.c_str(), config_link.c_str()) != 0) {
    LOG(ERROR) << "Failed to create symlink to config file at " << config_link
               << ": " << strerror(errno);
    return false;
  }

  return true;
}

#ifndef O_TMPFILE
# define O_TMPFILE (020000000 | O_DIRECTORY)
#endif

const CuttlefishConfig* InitFilesystemAndCreateConfig(
    FetcherConfig fetcher_config, KernelConfig kernel_config,
    fruit::Injector<>& injector) {
  std::string assembly_dir_parent = AbsolutePath(FLAGS_assembly_dir);
  while (assembly_dir_parent[assembly_dir_parent.size() - 1] == '/') {
    assembly_dir_parent =
        assembly_dir_parent.substr(0, FLAGS_assembly_dir.rfind('/'));
  }
  assembly_dir_parent =
      assembly_dir_parent.substr(0, FLAGS_assembly_dir.rfind('/'));
  auto log =
      SharedFD::Open(
          assembly_dir_parent,
          O_WRONLY | O_TMPFILE,
          S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  if (!log->IsOpen()) {
    LOG(ERROR) << "Could not open O_TMPFILE precursor to assemble_cvd.log: "
               << log->StrError();
  } else {
    android::base::SetLogger(TeeLogger({
        {ConsoleSeverity(), SharedFD::Dup(2), MetadataLevel::ONLY_MESSAGE},
        {LogFileSeverity(), log, MetadataLevel::FULL},
    }));
  }

  {
    // The config object is created here, but only exists in memory until the
    // SaveConfig line below. Don't launch cuttlefish subprocesses between these
    // two operations, as those will assume they can read the config object from
    // disk.
    auto config = InitializeCuttlefishConfiguration(FLAGS_instance_dir,
                                                    FLAGS_modem_simulator_count,
                                                    kernel_config, injector);
    std::set<std::string> preserving;
    bool create_os_composite_disk = ShouldCreateOsCompositeDisk(config);
    if (FLAGS_resume && create_os_composite_disk) {
      LOG(INFO) << "Requested resuming a previous session (the default behavior) "
                << "but the base images have changed under the overlay, making the "
                << "overlay incompatible. Wiping the overlay files.";
    } else if (FLAGS_resume && !create_os_composite_disk) {
      preserving.insert("overlay.img");
      preserving.insert("ap_overlay.img");
      preserving.insert("os_composite_disk_config.txt");
      preserving.insert("os_composite_gpt_header.img");
      preserving.insert("os_composite_gpt_footer.img");
      preserving.insert("os_composite.img");
      preserving.insert("sdcard.img");
      preserving.insert("boot_repacked.img");
      preserving.insert("vendor_boot_repacked.img");
      preserving.insert("access-kregistry");
      preserving.insert("NVChip");
      preserving.insert("gatekeeper_secure");
      preserving.insert("gatekeeper_insecure");
      preserving.insert("modem_nvram.json");
      preserving.insert("recording");
      preserving.insert("persistent_composite_disk_config.txt");
      preserving.insert("persistent_composite_gpt_header.img");
      preserving.insert("persistent_composite_gpt_footer.img");
      preserving.insert("persistent_composite.img");
      preserving.insert("uboot_env.img");
      preserving.insert("factory_reset_protected.img");
      std::stringstream ss;
      for (int i = 0; i < FLAGS_modem_simulator_count; i++) {
        ss.clear();
        ss << "iccprofile_for_sim" << i << ".xml";
        preserving.insert(ss.str());
        ss.str("");
      }
    }
    CHECK(
        CleanPriorFiles(preserving, FLAGS_assembly_dir, config.instance_dirs()))
        << "Failed to clean prior files";

    // Create assembly directory if it doesn't exist.
    CHECK(EnsureDirectoryExists(FLAGS_assembly_dir));
    if (log->LinkAtCwd(config.AssemblyPath("assemble_cvd.log"))) {
      LOG(ERROR) << "Unable to persist assemble_cvd log at "
                  << config.AssemblyPath("assemble_cvd.log")
                  << ": " << log->StrError();
    }

    auto disk_config = GetOsCompositeDiskConfig();
    if (auto it = std::find_if(disk_config.begin(), disk_config.end(),
                               [](const auto& partition) {
                                 return partition.label == "ap_rootfs";
                               });
        it != disk_config.end()) {
      auto ap_image_idx = std::distance(disk_config.begin(), it) + 1;
      std::stringstream ss;
      ss << "/dev/vda" << ap_image_idx;
      config.set_ap_image_dev_path(ss.str());
    }
    for (const auto& instance : config.Instances()) {
      // Create instance directory if it doesn't exist.
      CHECK(EnsureDirectoryExists(instance.instance_dir()));
      auto internal_dir = instance.instance_dir() + "/" + kInternalDirName;
      CHECK(EnsureDirectoryExists(internal_dir));
      auto shared_dir = instance.instance_dir() + "/" + kSharedDirName;
      CHECK(EnsureDirectoryExists(shared_dir));
      auto recording_dir = instance.instance_dir() + "/recording";
      CHECK(EnsureDirectoryExists(recording_dir));
    }
    CHECK(SaveConfig(config)) << "Failed to initialize configuration";
  }

  std::string first_instance = FLAGS_instance_dir + "." + std::to_string(GetInstance());
  if (FileExists(FLAGS_instance_dir)) {
    CHECK(RemoveFile(FLAGS_instance_dir))
        << "Failed to remove instance_dir symlink " << FLAGS_instance_dir;
  }
  CHECK_EQ(symlink(first_instance.c_str(), FLAGS_instance_dir.c_str()), 0)
      << "Could not symlink \"" << first_instance << "\" to \""
      << FLAGS_instance_dir << "\"";

  // Do this early so that the config object is ready for anything that needs it
  auto config = CuttlefishConfig::Get();
  CHECK(config) << "Failed to obtain config singleton";

  CreateDynamicDiskFiles(fetcher_config, *config);

  return config;
}

const std::string kKernelDefaultPath = "kernel";
const std::string kInitramfsImg = "initramfs.img";
static void ExtractKernelParamsFromFetcherConfig(
    const FetcherConfig& fetcher_config) {
  std::string discovered_kernel =
      fetcher_config.FindCvdFileWithSuffix(kKernelDefaultPath);
  std::string discovered_ramdisk =
      fetcher_config.FindCvdFileWithSuffix(kInitramfsImg);

  SetCommandLineOptionWithMode("kernel_path", discovered_kernel.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);

  SetCommandLineOptionWithMode("initramfs_path", discovered_ramdisk.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
}

fruit::Component<> FlagsComponent() {
  return fruit::createComponent()
      .install(AdbConfigComponent)
      .install(AdbConfigFlagComponent)
      .install(AdbConfigFragmentComponent)
      .install(GflagsComponent)
      .install(ConfigFlagComponent)
      .install(CustomActionsComponent);
}

} // namespace

int AssembleCvdMain(int argc, char** argv) {
  setenv("ANDROID_LOG_TAGS", "*:v", /* overwrite */ 0);
  ::android::base::InitLogging(argv, android::base::StderrLogger);

  int tty = isatty(0);
  int error_num = errno;
  CHECK_EQ(tty, 0)
      << "stdin was a tty, expected to be passed the output of a previous stage. "
      << "Did you mean to run launch_cvd?";
  CHECK(error_num != EBADF)
      << "stdin was not a valid file descriptor, expected to be passed the output "
      << "of launch_cvd. Did you mean to run launch_cvd?";

  std::string input_files_str;
  {
    auto input_fd = SharedFD::Dup(0);
    auto bytes_read = ReadAll(input_fd, &input_files_str);
    CHECK(bytes_read >= 0)
        << "Failed to read input files. Error was \"" << input_fd->StrError() << "\"";
  }
  std::vector<std::string> input_files = android::base::Split(input_files_str, "\n");

  FetcherConfig fetcher_config = FindFetcherConfig(input_files);
  // set gflags defaults to point to kernel/RD from fetcher config
  ExtractKernelParamsFromFetcherConfig(fetcher_config);

  KernelConfig kernel_config;
  auto args = ArgsToVec(argc - 1, argv + 1);

  bool help = false;
  std::string help_str;
  bool helpxml = false;

  std::vector<Flag> help_flags = {
      GflagsCompatFlag("help", help),
      GflagsCompatFlag("helpfull", help),
      GflagsCompatFlag("helpshort", help),
      GflagsCompatFlag("helpmatch", help_str),
      GflagsCompatFlag("helpon", help_str),
      GflagsCompatFlag("helppackage", help_str),
      GflagsCompatFlag("helpxml", helpxml),
  };
  for (const auto& help_flag : help_flags) {
    if (!help_flag.Parse(args)) {
      LOG(ERROR) << "Failed to process help flag.";
      return 1;
    }
  }

  fruit::Injector<> injector(FlagsComponent);
  auto flag_features = injector.getMultibindings<FlagFeature>();
  if (!FlagFeature::ProcessFlags(flag_features, args)) {
    LOG(ERROR) << "Failed to parse flags.";
    return 1;
  }

  if (help || help_str != "") {
    LOG(WARNING) << "TODO(schuffelen): Implement `--help` for assemble_cvd.";
    LOG(WARNING) << "In the meantime, call `launch_cvd --help`";
    return 1;
  } else if (helpxml) {
    if (!FlagFeature::WriteGflagsHelpXml(flag_features, std::cout)) {
      LOG(ERROR) << "Failure in writing gflags helpxml output";
    }
    std::exit(1);  // For parity with gflags
  }
  // TODO(schuffelen): Put in "unknown flag" guards after gflags is removed.
  // gflags either consumes all arguments that start with - or leaves all of
  // them in place, and either errors out on unknown flags or accepts any flags.

  CHECK(GetKernelConfigAndSetDefaults(&kernel_config))
      << "Failed to parse arguments";

  auto config = InitFilesystemAndCreateConfig(std::move(fetcher_config),
                                              kernel_config, injector);

  std::cout << GetConfigFilePath(*config) << "\n";
  std::cout << std::flush;

  return 0;
}

} // namespace cuttlefish

int main(int argc, char** argv) {
  return cuttlefish::AssembleCvdMain(argc, argv);
}
