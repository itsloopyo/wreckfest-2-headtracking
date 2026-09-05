// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cameraunlock/logging/file_log.h>

// The process-wide log lives in cameraunlock-core (logging::Open/Close/Line/
// EmergencyLine). Alias it under the mod namespace so call sites read
// Log::Line(...) unqualified.
namespace wf2_ht
{
    namespace Log = ::cameraunlock::logging;
}
