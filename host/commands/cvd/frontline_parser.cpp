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

#include "host/commands/cvd/frontline_parser.h"

#include <sstream>
#include <type_traits>
#include <vector>

#include <android-base/file.h>
#include <android-base/strings.h>

#include "common/libs/utils/flag_parser.h"
#include "host/commands/cvd/selector/selector_constants.h"

namespace cuttlefish {

struct BoolFlags {
  std::unordered_set<std::string> selector_flags;
  std::unordered_set<std::string> cvd_driver_flags;
};

struct ValueFlags {
  std::unordered_set<std::string> selector_flags;
  std::unordered_set<std::string> cvd_driver_flags;
};

static const BoolFlags bool_flags{
    .selector_flags = {selector::SelectorFlags::kDisableDefaultGroup,
                       selector::SelectorFlags::kAcquireFileLock},
    .cvd_driver_flags = {"clean", "help"}};

static const ValueFlags value_flags{
    .selector_flags = {selector::SelectorFlags::kGroupName,
                       selector::SelectorFlags::kInstanceName},
    .cvd_driver_flags = {}};

static std::unordered_set<std::string> Merge(
    const std::unordered_set<std::string>& s1,
    const std::unordered_set<std::string>& s2) {
  std::unordered_set<std::string> out{s1};
  out.insert(s2.begin(), s2.end());
  return out;
}

Result<std::unique_ptr<FrontlineParser>> FrontlineParser::Parse(
    ParserParam param) {
  CF_EXPECT(!param.all_args.empty());
  // TODO(kwstephenkim): implement these ad-hoc help checking in the
  // parser.
  if (android::base::Basename(param.all_args[0]) == "cvd") {
    if (param.all_args.size() == 1) {
      param.all_args.emplace_back("--help");
    }
    if (param.all_args.at(1) == "-h") {
      param.all_args[1] = "--help";
    }
  }

  FrontlineParser* frontline_parser = new FrontlineParser(param);
  CF_EXPECT(frontline_parser != nullptr,
            "Memory allocation for FrontlineParser failed.");
  CF_EXPECT(frontline_parser->Separate());
  return std::unique_ptr<FrontlineParser>(frontline_parser);
}

FrontlineParser::FrontlineParser(const ParserParam& param)
    : server_supported_subcmds_{param.server_supported_subcmds},
      all_args_(param.all_args),
      internal_cmds_(param.internal_cmds) {
  known_bool_flags_ =
      Merge(bool_flags.selector_flags, bool_flags.cvd_driver_flags);
  known_value_flags_ =
      Merge(value_flags.selector_flags, value_flags.cvd_driver_flags);
  selector_flags_ =
      Merge(bool_flags.selector_flags, value_flags.selector_flags);
}

Result<void> FrontlineParser::Separate() {
  arguments_separator_ = CF_EXPECT(CallSeparator());
  auto filtered_output = CF_EXPECT(FilterNonSelectorArgs());
  clean_ = filtered_output.clean;
  help_ = filtered_output.help;
  selector_args_ = std::move(filtered_output.selector_args);
  return {};
}

Result<cvd_common::Args> FrontlineParser::ValidSubcmdsList() {
  cvd_common::Args valid_subcmds(server_supported_subcmds_);
  std::copy(internal_cmds_.cbegin(), internal_cmds_.cend(),
            std::back_inserter(valid_subcmds));
  return valid_subcmds;
}

Result<std::unique_ptr<selector::ArgumentsSeparator>>
FrontlineParser::CallSeparator() {
  auto valid_subcmds_vector = CF_EXPECT(ValidSubcmdsList());
  std::unordered_set<std::string> valid_subcmds{valid_subcmds_vector.begin(),
                                                valid_subcmds_vector.end()};
  ArgumentsSeparator::FlagsRegistration flag_registration{
      .known_boolean_flags = known_bool_flags_,
      .known_value_flags = known_value_flags_,
      .valid_subcommands = valid_subcmds};
  auto arguments_separator =
      CF_EXPECT(ArgumentsSeparator::Parse(flag_registration, all_args_));
  CF_EXPECT(arguments_separator != nullptr);
  return arguments_separator;
}

const std::string& FrontlineParser::ProgPath() const {
  return arguments_separator_->ProgPath();
}

std::optional<std::string> FrontlineParser::SubCmd() const {
  return arguments_separator_->SubCmd();
}

const cvd_common::Args& FrontlineParser::SubCmdArgs() const {
  return arguments_separator_->SubCmdArgs();
}

Result<FrontlineParser::FilterOutput> FrontlineParser::FilterNonSelectorArgs() {
  auto cvd_args = arguments_separator_->CvdArgs();
  bool clean = false;
  bool help = false;
  std::vector<Flag> gflags;
  gflags.emplace_back(GflagsCompatFlag("clean", clean));
  gflags.emplace_back(GflagsCompatFlag("help", help));
  CF_EXPECT(ParseFlags(gflags, cvd_args));
  FilterOutput output{
      .clean = clean, .help = help, .selector_args = std::move(cvd_args)};
  return output;
}

const cvd_common::Args& FrontlineParser::SelectorArgs() const {
  return selector_args_;
}

}  // namespace cuttlefish