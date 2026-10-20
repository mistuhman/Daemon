#pragma once

// Release identity is injected by CMake. Standalone syntax/harness builds keep
// an explicit development label instead of pretending to be a shipped build.
#ifndef DAEMON_ITERATION
#define DAEMON_ITERATION "ti-dev"
#endif

#define DAEMON_DISPLAY_NAME "Daemon " DAEMON_ITERATION
