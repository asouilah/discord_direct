#pragma once

// Installs inline hooks on ws2_32.dll's sendto/WSASendTo so outgoing UDP
// traffic can be lightly padded before Discord's real voice packets go out.
// Returns true if at least one hook was installed successfully.
bool InstallDirectModeHooks();

// Removes any hooks previously installed and frees trampoline memory.
void RemoveDirectModeHooks();
