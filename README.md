# macbook8-1-spi-resume

Fixes the internal keyboard and touchpad not working after suspend (S3)
on the 12" MacBook 2015 (MacBook8,1) under Linux.

## Symptom

After resume, the keyboard/touchpad are dead and the kernel log fills with:

    applespi spi-APP000D:00: SPI transfer timed out
    applespi spi-APP000D:00: Error reading from device: -110

This is the long-standing "Resume on MacBook8,1" limitation listed by the
original macbook12-spi-driver project.

## Root cause

The SPI controller (Intel Wildcat Point-LP LPSS, PCI `8086:9ce6`, `00:15.4`)
runs in PCI mode on this machine. Its LPSS private registers at
`BAR0 + 0x800` lose their contents in S3:

| Register | Offset | Before S3 | After S3 |
|---|---|---|---|
| PRV_CLOCK  | 0x800 | `0x80190005` (enabled, M/N = 2/25) | `0x00000000` |
| PRV_RESETS | 0x804 | `0x00000003` (out of reset)        | `0x00000000` |
| PRV_CS_CTRL| 0x818 | `0x00000003`                       | `0x00000000` |

The SSP block is left in reset with its clock gated, so every SPI transfer
times out. Apple's firmware does not restore these registers on S3 resume,
and `spi-pxa2xx` does not restore them either for LPT/WPT in PCI mode
(`acpi_lpss` only covers ACPI mode). PCI remove/rescan does not help since
probe does not touch these registers. Writing the saved values back
restores the keyboard immediately, which confirms causality.

## How it works

`lpss_ctx_restore` registers a platform device with late/early system-sleep
callbacks. It saves the private registers in `suspend_late` and restores
them in `resume_early`. PM phases are global barriers, so this runs after
PCI config space is restored (`resume_noirq`) and before `spi-pxa2xx` and
`applespi` resume. No userspace hooks are needed. Registers are only written
if they were actually lost. The module refuses to load on anything other
than MacBook8,1 (DMI check, override with `force=1`).

## Installation

### NixOS (flake)

```nix
inputs.mb81-spi.url = "github:jankowskirobert/macbook8-1-spi-resume";
inputs.mb81-spi.inputs.nixpkgs.follows = "nixpkgs";

# in your nixosConfiguration modules:
inputs.mb81-spi.nixosModules.default
{ hardware.macbook8-1.spiResume.enable = true; }
```

### Debian / Ubuntu (DKMS)

```sh
sudo apt install dkms build-essential linux-headers-$(uname -r)
sudo cp -r module /usr/src/lpss-ctx-restore-0.1.0
sudo dkms install lpss-ctx-restore/0.1.0
echo lpss_ctx_restore | sudo tee /etc/modules-load.d/lpss-ctx-restore.conf
```

Recommended: add `module_blacklist=dw_dmac_pci` to the kernel command line
(PIO mode; the DMA path has not been validated across S3).

## Verification

```sh
lsmod | grep lpss_ctx_restore
journalctl -k -b | grep -E 'lpss_ctx_restore|context restored'
```

## Status

- Validation: in progress
- Upstream fix for `spi-pxa2xx`: planned

## License

GPL-2.0
