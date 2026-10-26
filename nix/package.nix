{ lib, stdenv, kernel }:

stdenv.mkDerivation {
  pname = "lpss-ctx-restore";
  version = "0.1.0-${kernel.version}";
  src = ../module;

  nativeBuildInputs = kernel.moduleBuildDependencies;
  makeFlags = [
    "KDIR=${kernel.dev}/lib/modules/${kernel.modDirVersion}/build"
  ];

  installPhase = ''
    runHook preInstall
    install -D lpss_ctx_restore.ko \
      $out/lib/modules/${kernel.modDirVersion}/extra/lpss_ctx_restore.ko
    runHook postInstall
  '';

  meta = {
    description = "Restore Intel LPSS SPI private context after sleep on MacBook8,1";
    license = lib.licenses.gpl2Only;
    platforms = [ "x86_64-linux" ];
  };
}
