#pragma once
namespace galaxy {
// Stop producers on the game thread first; join workers before releasing platform resources.
void shutdownOsServices();
void shutdownAlarms();
}
