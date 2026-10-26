{
  description = "MacBook8,1: restore LPSS SPI context after sleep (keyboard/touchpad)";
  outputs = { self }: {
    nixosModules.default = import ./nix/module.nix;
  };
}
