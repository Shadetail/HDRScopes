// Startup check for a newer GitHub Release (see the .cpp for how the version
// is fetched). The check runs on a background thread and never blocks.
#pragma once

#include <string>

namespace updatecheck {

// Fire-and-forget: starts the background check (at most once per process).
void StartAsync();

// True once a finished check found a release newer than the running build;
// fills the release tag ("v1.2.3") and its release-page URL.
bool NewerAvailable(std::string* tag, std::string* url);

}
