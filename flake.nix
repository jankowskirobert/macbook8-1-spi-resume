{
  description = "MacBook8,1: restore Intel LPSS SPI context after sleep (keyboard/touchpad)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in
    {
      nixosModules.default = import ./nix/module.nix;
      packages.${system}.default =
        pkgs.linuxPackages.callPackage ./nix/package.nix { };
      checks.${system}.build = self.packages.${system}.default;
    };
}
