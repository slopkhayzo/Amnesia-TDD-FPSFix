AmnesiaTDD.FPSFix 1.0.1
=======================

AmnesiaTDD.FPSFix decouples presentation from Amnesia: The Dark Descent's
fixed 60 Hz simulation. It provides presentation-rate camera and mouse input,
first-person hands and lantern motion, dynamic props, drawers, doors, cloth,
enemies, ragdolls, particles, and frame-rate-correct image-trail effects.

COMPATIBILITY
-------------

Amnesia.exe must be a 32-bit Windows / PE32 executable whose required engine
functions uniquely match every validated code signature and whose required
imports are present. The plugin does not require a particular file hash,
storefront, installation path, file size, or code RVA. If any required mapping
is absent, ambiguous, malformed, or non-executable, startup fails closed before
engine hooks are installed. This compatibility scan runs once during deferred
plugin startup and is never repeated in simulation, input, or render paths.

Validated release builds:

GOG Windows build ID: 53742270235201755
Amnesia.exe SHA-256:
E75EE41394B2D20B0F96B74713C40F6C38BAC88E98D0E23383036F22EA1688D0

Steam (2023 update), supported since 1.0.1:
Amnesia.exe SHA-256:
FB5E25DD463983D50C3986A17856F7A6DB63F14D73D9AAF754E0B94A894521EE
Amnesia_NoSteam.exe SHA-256:
FD90F172BCCD1F3BAF672694952351B8C30EC308618BDFB3F902D9415E5C1011

Other storefronts and executable revisions are allowed by the compatibility
scanner, but are unverified until tested. A successful signature scan means
that the executable has the exact code and layout assumptions used by 1.0.1;
it is not a general promise that every revision will match.

INSTALLATION
------------

Plugin-only package:

1. Install a compatible x86 ASI loader for Amnesia.
2. Copy AmnesiaTDD.FPSFix.asi and AmnesiaTDD.FPSFix.ini together beside
   Amnesia.exe, or to another directory scanned by that loader.

Package bundled with Ultimate ASI Loader:

1. Copy AmnesiaTDD.FPSFix.asi, AmnesiaTDD.FPSFix.ini, and wininet.dll beside
   Amnesia.exe.
2. Do not overwrite an existing wininet.dll. If one is already present, find
   out which mod owns it and use the plugin-only package with the existing
   compatible x86 ASI loader instead.

The bundled wininet.dll is the verified x86 Ultimate ASI Loader v9.7.4 binary.
Amnesia is a 32-bit game, so a 64-bit loader will not work.

USAGE
-----

Keep the game's VSync enabled or apply an external frame-rate cap. The plugin's
internal presentation limiter is not implemented, and TargetFPS=0 is uncapped.
The original game simulation remains fixed at 60 Hz by design.

The supplied INI enables all validated presentation fixes. Most users should
leave it unchanged. Diagnostics write a lightweight status report every five
seconds; the heavier MotionTrace capture is disabled by default.

Logs are normally written beside the plugin as AmnesiaTDD.FPSFix.log. If that
location is not writable, the fallback is:

%LOCALAPPDATA%\AmnesiaTDD.FPSFix\AmnesiaTDD.FPSFix.log

UNINSTALLATION
--------------

Remove AmnesiaTDD.FPSFix.asi and AmnesiaTDD.FPSFix.ini. If this package added
wininet.dll, remove it only when no other installed ASI plugin depends on that
loader. Log files can be removed separately.

FILES AND THIRD-PARTY SOFTWARE
------------------------------

SHA256SUMS.txt lists hashes for every payload file. MINHOOK-LICENSE.txt covers
the statically linked MinHook dependency. The bundled-loader package also
includes THIRD-PARTY-NOTICES.txt for Ultimate ASI Loader.

This software is provided without warranty. Back up modified game files and
save data before installing game modifications.


Misc
-------
For more game fixes, visit https://slop-blog.enkhayzomachines.net/fixes :)
