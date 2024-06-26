//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include "absl/log/log.h"

#include <stdio.h>
#include <string.h>

#include "absl/log/globals.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>

#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/util/string.h"

#ifndef GPR_DEFAULT_LOG_VERBOSITY_STRING
#define GPR_DEFAULT_LOG_VERBOSITY_STRING "ERROR"
#endif  // !GPR_DEFAULT_LOG_VERBOSITY_STRING

static constexpr gpr_atm GPR_LOG_SEVERITY_UNSET = GPR_LOG_SEVERITY_ERROR + 10;
static constexpr gpr_atm GPR_LOG_SEVERITY_NONE = GPR_LOG_SEVERITY_ERROR + 11;

void gpr_default_log(gpr_log_func_args* args);
void gpr_platform_log(gpr_log_func_args* args);
static gpr_atm g_log_func = reinterpret_cast<gpr_atm>(gpr_default_log);
static gpr_atm g_min_severity_to_print = GPR_LOG_SEVERITY_UNSET;
static gpr_atm g_min_severity_to_print_stacktrace = GPR_LOG_SEVERITY_UNSET;

void gpr_unreachable_code(const char* reason, const char* file, int line) {
  grpc_core::Crash(absl::StrCat("UNREACHABLE CODE: ", reason),
                   grpc_core::SourceLocation(file, line));
}

const char* gpr_log_severity_string(gpr_log_severity severity) {
  switch (severity) {
    case GPR_LOG_SEVERITY_DEBUG:
      return "D";
    case GPR_LOG_SEVERITY_INFO:
      return "I";
    case GPR_LOG_SEVERITY_ERROR:
      return "E";
  }
  GPR_UNREACHABLE_CODE(return "UNKNOWN");
}

int gpr_should_log(gpr_log_severity severity) {
  return static_cast<gpr_atm>(severity) >=
                 gpr_atm_no_barrier_load(&g_min_severity_to_print)
             ? 1
             : 0;
}

void gpr_default_log(gpr_log_func_args* args) {
  switch (args->severity) {
    case GPR_LOG_SEVERITY_DEBUG:
      //  Log DEBUG messages as VLOG(2).
      VLOG(2).AtLocation(args->file, args->line) << args->message;
      return;
    case GPR_LOG_SEVERITY_INFO:
      LOG(INFO).AtLocation(args->file, args->line) << args->message;
      return;
    case GPR_LOG_SEVERITY_ERROR:
      LOG(ERROR).AtLocation(args->file, args->line) << args->message;
      return;
    default:
      LOG(ERROR) << __func__ << ": unknown gpr log severity(" << args->severity
                 << "), using ERROR";
      LOG(ERROR).AtLocation(args->file, args->line) << args->message;
  }
}

void gpr_log_message(const char* file, int line, gpr_log_severity severity,
                     const char* message) {
  if (gpr_should_log(severity) == 0) {
    return;
  }

  gpr_log_func_args lfargs;
  memset(&lfargs, 0, sizeof(lfargs));
  lfargs.file = file;
  lfargs.line = line;
  lfargs.severity = severity;
  lfargs.message = message;
  reinterpret_cast<gpr_log_func>(gpr_atm_no_barrier_load(&g_log_func))(&lfargs);
}

void gpr_set_log_verbosity(gpr_log_severity min_severity_to_print) {
  gpr_atm_no_barrier_store(&g_min_severity_to_print,
                           (gpr_atm)min_severity_to_print);
}

static gpr_atm parse_log_severity(absl::string_view str, gpr_atm error_value) {
  if (absl::EqualsIgnoreCase(str, "DEBUG")) return GPR_LOG_SEVERITY_DEBUG;
  if (absl::EqualsIgnoreCase(str, "INFO")) return GPR_LOG_SEVERITY_INFO;
  if (absl::EqualsIgnoreCase(str, "ERROR")) return GPR_LOG_SEVERITY_ERROR;
  if (absl::EqualsIgnoreCase(str, "NONE")) return GPR_LOG_SEVERITY_NONE;
  return error_value;
}

void gpr_to_absl_verbosity_setting_init(void) {
// This is enabled in Github only.
// This ifndef is converted to ifdef internally by copybara.
// Internally grpc verbosity is managed using absl settings.
// So internally we avoid setting it like this.
#ifndef GRPC_VERBOSITY_MACRO
  absl::string_view verbosity = grpc_core::ConfigVars::Get().Verbosity();
  DVLOG(2) << "Log verbosity: " << verbosity;
  if (absl::EqualsIgnoreCase(verbosity, "INFO")) {
    LOG(WARNING) << "Not suitable for production. Prefer WARNING or ERROR. "
                    "However if you see this message in a debug environmenmt "
                    "or test environmenmt it is safe to ignore this message.";
    absl::SetVLogLevel("*grpc*/*", -1);
    absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  } else if (absl::EqualsIgnoreCase(verbosity, "DEBUG")) {
    LOG(WARNING) << "Not suitable for production. Prefer WARNING or ERROR. "
                    "However if you see this message in a debug environmenmt "
                    "or test environmenmt it is safe to ignore this message.";
    absl::SetVLogLevel("*grpc*/*", 2);
    absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  } else if (absl::EqualsIgnoreCase(verbosity, "ERROR")) {
    absl::SetVLogLevel("*grpc*/*", -1);
    absl::SetMinLogLevel(absl::LogSeverityAtLeast::kError);
  } else if (absl::EqualsIgnoreCase(verbosity, "NONE")) {
    absl::SetVLogLevel("*grpc*/*", -1);
    absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfinity);
  } else {
    LOG(ERROR) << "Unknown log verbosity: " << verbosity;
  }
#endif  // GRPC_VERBOSITY_MACRO
}

void gpr_log_verbosity_init() {
  // init verbosity when it hasn't been set
  if ((gpr_atm_no_barrier_load(&g_min_severity_to_print)) ==
      GPR_LOG_SEVERITY_UNSET) {
    auto verbosity = grpc_core::ConfigVars::Get().Verbosity();
    gpr_atm min_severity_to_print = GPR_LOG_SEVERITY_ERROR;
    if (!verbosity.empty()) {
      min_severity_to_print =
          parse_log_severity(verbosity, min_severity_to_print);
    }
    gpr_atm_no_barrier_store(&g_min_severity_to_print, min_severity_to_print);
  }
  // init stacktrace_minloglevel when it hasn't been set
  if ((gpr_atm_no_barrier_load(&g_min_severity_to_print_stacktrace)) ==
      GPR_LOG_SEVERITY_UNSET) {
    auto stacktrace_minloglevel =
        grpc_core::ConfigVars::Get().StacktraceMinloglevel();
    gpr_atm min_severity_to_print_stacktrace = GPR_LOG_SEVERITY_NONE;
    if (!stacktrace_minloglevel.empty()) {
      min_severity_to_print_stacktrace = parse_log_severity(
          stacktrace_minloglevel, min_severity_to_print_stacktrace);
    }
    gpr_atm_no_barrier_store(&g_min_severity_to_print_stacktrace,
                             min_severity_to_print_stacktrace);
  }
  gpr_to_absl_verbosity_setting_init();
}

void gpr_set_log_function(gpr_log_func f) {
  LOG(ERROR)
      << "This function is in the process of being deprecated. Your "
         "gpr_log_func will not work as expected. This is because only few "
         "instances of gpr_log remain in our code base. All instances of "
         "gpr_log will log via this custom function. Many of the gpr_log "
         "statements have been converted to ABSL_LOG statements. These will "
         "log to the default absl log sink. The gpr_set_log_function function "
         "will be deleted in the next gRPC release. We strongly advice against "
         "using this function. You may create a new absl LogSink with similar "
         "functionality. gRFC: https://github.com/grpc/proposal/pull/425 ";
  gpr_atm_no_barrier_store(&g_log_func, (gpr_atm)(f ? f : gpr_default_log));
}
