{
  description = "A multi-platform flake for compiling .kpm files with LLVM dependency and devshell";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = {
    self,
    nixpkgs,
    flake-utils,
  }:
    flake-utils.lib.eachDefaultSystem (system: let
      pkgs = import nixpkgs {inherit system;};

      # List directories excluding "KernelPatch" and ensuring they have a Makefile
      listKpmDirs =
        builtins.filter
        (dir: dir != "KernelPatch" && builtins.pathExists (self + "/" + dir + "/Makefile"))
        (builtins.attrNames (builtins.readDir self));

      # Generate individual KPM module packages
      modulePackages = builtins.listToAttrs (builtins.map (folder: {
          name = folder;
          value = pkgs.stdenv.mkDerivation rec {
            pname = folder;
            version = "1.0";

            src = self + "/" + folder;

            buildInputs = [pkgs.llvm pkgs.clang pkgs.makeWrapper];

            buildPhase = ''
              set -e
              make -C ${src}
            '';

            installPhase = ''
              mkdir -p $out/kpm_packages/${folder}
              find . -name "*.kpm" -exec mv {} $out/kpm_packages/${folder} \;
            '';

            meta = {
              description = "Compiled KPM module for ${folder}";
              platforms = pkgs.lib.platforms.linux;
            };
          };
        })
        listKpmDirs);

      # Combined package for all KPM modules
      allKpmPackage = pkgs.stdenv.mkDerivation {
        pname = "all-kpm";
        version = "1.0";

        nativeBuildInputs = builtins.attrValues modulePackages;

        buildPhase = ''
          mkdir -p $out/kpm_packages
          for dir in ${toString listKpmDirs}; do
          done
        '';

        meta = {
          description = "Combined package of all KPM modules";
          platforms = pkgs.lib.platforms.linux;
        };
      };

      # Development shell with LLVM and tools
      devShell = pkgs.mkShell {
        buildInputs = [pkgs.llvm pkgs.clang pkgs.makeWrapper pkgs.git pkgs.cmake pkgs.gcc];
        shellHook = ''
          echo "Welcome to the KPM DevShell! LLVM, Clang, GCC, and build tools are available."
        '';
      };
    in {
      # Individual packages for each KPM module
      packages =
        builtins.listToAttrs (builtins.map (folder: {
            name = folder;
            value = pkgs.lib.getAttr folder modulePackages;
          })
          listKpmDirs)
        // {"all-kpm" = allKpmPackage;};

      devShell = devShell;
    });
}
