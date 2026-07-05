# Discord Direct

For educational purposes only.

Lets Discord voice chat get through local network restrictions (like the VoIP blocking some ISPs apply, e.g. in the UAE) without a VPN or proxy.

## Features

- No proxy, no VPN. Works at the process level, only affects Discord.
- Single exe. Nothing else to download or place manually.
- Auto-detects your Discord install folder.
- Checks if Discord is running before doing anything, and can close it for you.
- No drivers, no system-wide changes. Uninstall reverts everything.

## How it works

Discord doesn't let you route its traffic through anything, and on some networks its voice chat specifically gets fingerprinted and blocked by the ISP while everything else about Discord works fine.

This gets around that in two parts.

**1. Getting code to run alongside Discord.**
Windows loads a DLL named `version.dll` from an app's own folder before checking the system one. Discord doesn't ship its own, so dropping one next to `Discord.exe` gets it loaded automatically on next launch. No injector or driver needed, just a file in a folder.

**2. Messing with the first packet of each voice connection.**
Once loaded, it intercepts Discord's outgoing voice traffic. The first time a new voice connection starts, it sends one extra packet of random data to the same destination before the real packet goes out. Real voice data is never touched, the receiving server just ignores the decoy. ISPs usually block based on recognizing the start of a connection, so throwing something unrecognizable in at that moment can be enough to slip past it.

## Installation

1. Download `DiscordDirect.exe`.
2. Fully close Discord (check the tray, not just the window).
3. Run it and click Install.
4. Open Discord and join a voice channel.

## Updating

Discord updates into a new folder sometimes. Just run Install again, it finds the current folder automatically.

## Uninstalling

Run it again and click Uninstall.

## Notes

Only handles the standard Discord client for now, not Canary/PTB.

Whether this actually gets past blocking on your network depends on how that network filters traffic. Not guaranteed to work everywhere.

For educational purposes only. Use of any network circumvention tool may be subject to local regulations, check that for yourself.
