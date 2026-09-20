# Credits

This fork was itself forked from [wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass), which builds on [Dagherbou/OptiScaler_DLSSNR](https://github.com/Dagherbou/OptiScaler_DLSSNR) and [OptiScaler](https://github.com/optiscaler/OptiScaler). OptiScaler began with [PotatoOfDoom's CyberFSR2](https://github.com/PotatoOfDoom/CyberFSR2).

Colour processing is derived from [RenoDX by clshortfuse](https://github.com/clshortfuse/renodx). See the [RenoDX attribution and licence](../Licenses/RenoDX_ATTRIBUTION.txt) for details.

## DLSS-NR exposure controls

Automatic exposure from the HDR frame, the wider exposure Trim range and the Trim Anchor points are @mattjaas's work, from [wilsjo2's PR #77](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/pull/77), adapted onto this fork's code.

## RTX 40 MFG unlock

The built-in RTX 40 multi frame generation unlock is adapted from [y4my4my4m's fork](https://github.com/y4my4my4m/OptiScaler_DLSSNR_Multipass_MFG) (GPL-3.0). The provider discovery, the Streamline plugin frame-ceiling patch, the software frame pacing option and the PTX temporal fix are adapted from [KleberMotta's fork](https://github.com/KleberMotta/OptiScaler-DLSS5-MFG-RTX40) (MIT), a port of the MFG Unlock ReShade addon by [Dreamt](https://github.com/ImDreamt/MFGAdaUnlock-RenoDx) and [mavismmg](https://github.com/mavismmg/MFGAdaUnlock-RenoDx). The technique originates from [dashdogy's RTX40MFG-Unlock](https://github.com/dashdogy/RTX40MFG-Unlock), which is also the optional external unlocker (see [RTX40-MFG.md](RTX40-MFG.md)). See the [licences](../Licenses/MFGUnlock_LICENSE.txt).

## OptiScaler contributors

These credits are retained from the original OptiScaler README:

- @PotatoOfDoom for CyberFSR2.
- @Artur for DLSS Enabler and help with the NVNGX API.
- @LukeFZ and @Nukem for their mods and shared knowledge.
- @FakeMichau for support, testing and features.
- @QM for testing and access to games.
- @TheRazerMD for testing and support.
- @Cryio, @krispy, @krisshietala, @Lordubuntu, @scz and @Veeqo for the earlier compatibility matrix.
- The DLSS2FSR community for its support.

This project uses [FreeType](https://gitlab.freedesktop.org/freetype/freetype), licensed under the [FTL](https://gitlab.freedesktop.org/freetype/freetype/-/blob/master/docs/FTL.TXT). Other notices are in [Licenses](../Licenses).

## Upstream sponsorship

The original OptiScaler project credits [SignPath.io](https://signpath.io/) for Windows code signing and the [SignPath Foundation](https://signpath.org/) for its certificate.

To support the original developers: [cdozdil on GitHub Sponsors](https://github.com/sponsors/cdozdil?frequency=one-time) and [nitec on Buy Me a Coffee](https://buymeacoffee.com/nitec).
