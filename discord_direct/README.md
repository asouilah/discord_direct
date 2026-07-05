# Discord Direct

> **For educational purposes only.**

A lightweight tool that lets Discord's voice chat get through local network
restrictions — like the VoIP blocking some ISPs apply (e.g. in the UAE) —
without a VPN, without a proxy, and without touching anything system-wide.

## Features

- **No proxy, no VPN.** Works purely at the process level, applied only to
  Discord.
- **Single executable.** Everything needed is packed into one installer —
  nothing else to download or place manually.
- **Auto-detects your Discord install.** Finds the current app folder under
  `%LOCALAPPDATA%\Discord` on its own.
- **Safe by default.** Checks whether Discord is running before doing
  anything, and offers to close it for you rather than failing silently.
- **No drivers, no system modifications.** Everything reverts cleanly with
  Uninstall.

## How it works

Discord doesn't expose a way to route its own network traffic through
anything, and on some networks its voice chat specifically gets
fingerprinted and blocked by the ISP, even though everything else about
Discord works fine.

Discord Direct works around this in two parts:

**1. Getting a small piece of code to run alongside Discord.**
Windows loads a DLL named `version.dll` from an app's own folder before
falling back to the system one — a normal feature of how Windows resolves
DLLs, and totally harmless as long as that DLL still behaves like the
real one. Discord doesn't ship its own `version.dll`, so placing one next
to `Discord.exe` gets it loaded automatically the next time Discord
starts. No injector, no driver, nothing invasive — just a file in a
folder.

**2. Slightly disrupting the first packet of each voice connection.**
Once loaded, it quietly intercepts Discord's outgoing voice traffic at the
network layer. The very first time a new voice connection starts, it
sends one extra packet of random data to the same destination *before*
the real voice packet goes out. Real voice data is never touched — only
this one decoy packet is added, and it's silently ignored by the
receiving server. The idea is that ISP filtering usually works by
pattern-matching the start of a connection to recognize "this is Discord
voice" and block it; throwing in something unrecognizable at that exact
moment can be enough to slip past that check, while everything that
follows continues to work as normal, real voice traffic.

## Installation

1. Download `DiscordDirect.exe`.
2. Fully close Discord — check the system tray, not just the main window.
3. Run `DiscordDirect.exe` and click **Install**.
   - If Discord is still running, it'll ask if you'd like it closed for
     you before continuing.
4. Open Discord and join a voice channel as usual.

That's it — no settings, no config file, nothing to type in.

## Updating

Discord periodically updates itself into a new folder. When that happens,
just run the installer again and click **Install** — it re-detects the
current folder automatically.

## Uninstalling

Run `DiscordDirect.exe` again and click **Uninstall**. This removes the
file and Discord goes back to behaving exactly as it did before.

## Notes

- Currently detects the standard Discord client. Canary/PTB support may
  come later.
- Whether this gets past voice blocking on your specific network depends
  on exactly how that network filters traffic — it's not guaranteed to
  work everywhere out of the box.
- This project is shared **for educational purposes only**, to demonstrate
  how DLL side-loading and lightweight traffic shaping work in practice.
  Use of any network circumvention tool may be subject to local
  regulations — that's on you to check for your own situation.
