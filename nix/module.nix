{ config, lib, ... }:
let
  cfg = config.hardware.macbook8-1.spiResume;
  lpssCtxRestore = config.boot.kernelPackages.callPackage ./package.nix { };
in
{
  options.hardware.macbook8-1.spiResume = {
    enable = lib.mkEnableOption
      "restoring LPSS SPI context after sleep on MacBook8,1 (keyboard/touchpad)";
    forcePio = lib.mkOption {
      type = lib.types.bool;
      default = true;
      description = ''
        Blacklist dw_dmac_pci so spi-pxa2xx uses PIO.
        The LPSS DMA function has not been validated across S3.
      '';
    };
  };

  config = lib.mkIf cfg.enable {
    boot.extraModulePackages = [ lpssCtxRestore ];
    boot.kernelModules = [ "lpss_ctx_restore" ];
    boot.initrd.kernelModules = [ "spi_pxa2xx_pci" "applespi" ];
    boot.kernelParams = lib.mkIf cfg.forcePio [ "module_blacklist=dw_dmac_pci" ];
  };
}
