{ config, lib, pkgs, ... }:
let
  cfg = config.hardware.macbook8-1.spiResume;
  kernel = config.boot.kernelPackages.kernel;
  lpssCtxRestore = pkgs.stdenv.mkDerivation {
    pname = "lpss-ctx-restore";
    version = "0.1.0";
    src = ../module;
    nativeBuildInputs = kernel.moduleBuildDependencies;
    makeFlags = [
      "KDIR=${kernel.dev}/lib/modules/${kernel.modDirVersion}/build"
    ];
    installPhase = ''
      install -D lpss_ctx_restore.ko \
        $out/lib/modules/${kernel.modDirVersion}/extra/lpss_ctx_restore.ko
    '';
  };
in
{
  options.hardware.macbook8-1.spiResume = {
    enable = lib.mkEnableOption
      "restoring LPSS SPI context after sleep on MacBook8,1 (keyboard/touchpad)";
    forcePio = lib.mkOption {
      type = lib.types.bool;
      default = true;
      description = "Blacklist dw_dmac_pci so spi-pxa2xx uses PIO; DMA path not validated across S3.";
    };
  };

  config = lib.mkIf cfg.enable {
    boot.extraModulePackages = [ lpssCtxRestore ];
    boot.kernelModules = [ "lpss_ctx_restore" ];
    boot.initrd.kernelModules = [ "spi_pxa2xx_pci" "applespi" ];
    boot.kernelParams = lib.mkIf cfg.forcePio [ "module_blacklist=dw_dmac_pci" ];
  };
}
