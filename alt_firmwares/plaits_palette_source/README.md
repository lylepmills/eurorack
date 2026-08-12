# `export_recipe_source.py`

Turn a saved Plaits Palette configuration into a local firmware source build
you can compile, edit, and keep hacking on.

## 1. Save your configuration

In [Plaits Palette](https://rubato.audio/plaits-palette), choose
**Save configuration**. You will get a file ending in
`.plaits-palette.json`.

## 2. Export the source build

You need Git and Python 3. In a terminal:

```sh
git clone --recurse-submodules https://github.com/lylepmills/eurorack.git
cd eurorack

python3 alt_firmwares/plaits_lab_builder/export_recipe_source.py \
  ~/Downloads/my-palette.plaits-palette.json \
  build/my-palette-source
```

Change the first path to the configuration file you downloaded.

The new `build/my-palette-source/` folder contains:

- your generated configuration headers and custom data;
- the exact source revision and build settings;
- a list of the source files used by your selected models;
- a build script for WAV or HEX firmware; and
- its own README with the build commands and notes about where to make changes.

## 3. Start hacking

Open:

```text
build/my-palette-source/README.md
```

It walks you through the reproducible build. Docker is the simplest way to get
the original ARM toolchain; the short version is:

```sh
docker build --platform linux/amd64 \
  -t mutable-eurorack-dev:local \
  -f .devcontainer/Dockerfile .

docker run --rm --platform linux/amd64 \
  -v "$PWD":/workspace -w /workspace \
  mutable-eurorack-dev:local \
  sh build/my-palette-source/build.sh wav
```

Your firmware will be at:

```text
build/my-palette-source/build/plaits/plaits.wav
```

Use `hex` instead of `wav` in the last command if you need an application-only
Intel HEX file for a hardware programmer.

The export is a small recipe-specific layer, not a duplicate of the whole
repository. Edit the actual firmware and model code in this checkout;
`build/my-palette-source/selected-models.md` points to the best places to start.
