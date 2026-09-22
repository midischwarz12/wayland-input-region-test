{
  description = "A small native Wayland input-region click-through test client";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in {
      packages = forAllSystems (system:
        let pkgs = nixpkgs.legacyPackages.${system};
        in {
          default = pkgs.stdenv.mkDerivation {
            pname = "wayland-input-region-test";
            version = "0.1.0";
            src = pkgs.lib.fileset.toSource {
              root = ./.;
              fileset = pkgs.lib.fileset.unions [ ./main.c ./Makefile ./tests/unit.c ./LICENSE ];
            };
            strictDeps = true;
            nativeBuildInputs = [ pkgs.pkg-config pkgs.wayland-scanner ];
            buildInputs = [ pkgs.wayland pkgs.wayland-protocols ];
            enableParallelBuilding = true;
            doCheck = true;
            checkTarget = "check";
            installPhase = ''
              runHook preInstall
              make install PREFIX="$out"
              runHook postInstall
            '';
            meta = {
              description = "Transparent, resizable Wayland input-region test client";
              homepage = "https://github.com/midischwarz12/wayland-input-region-test";
              license = pkgs.lib.licenses.mit;
              platforms = systems;
              mainProgram = "wayland-input-region-test";
            };
          };
        });
      apps = forAllSystems (system: {
        default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/wayland-input-region-test";
          meta.description = "Run the native Wayland click-through test client";
        };
      });
      checks = forAllSystems (system:
        let pkgs = nixpkgs.legacyPackages.${system};
        in {
          client = self.packages.${system}.default;
          protocol = pkgs.runCommand "wayland-input-region-protocol-check" {
            nativeBuildInputs = [ pkgs.weston pkgs.coreutils pkgs.gnugrep ];
          } ''
            bash ${./tests/protocol.sh} ${self.apps.${system}.default.program}
            touch "$out"
          '';
        });
      devShells = forAllSystems (system: {
        default = nixpkgs.legacyPackages.${system}.mkShell {
          inputsFrom = [ self.packages.${system}.default ];
        };
      });
    };
}
