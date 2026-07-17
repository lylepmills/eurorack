# Mutable Instruments firmware dev container

This configuration provides the legacy Mutable Instruments toolchain in an
AMD64 Linux container. It is intended for Apple Silicon Macs as well as other
Docker hosts.

## Prerequisites

- Docker Desktop. On Apple Silicon, enable **Use Rosetta for x86/amd64
  emulation** in Docker Desktop settings.
- VS Code with the **Dev Containers** extension.

Open this repository in VS Code, then select **Dev Containers: Reopen in
Container**. The first build downloads the original ARM GCC 4.8 toolchain.

The container builds the checkout opened in VS Code; it does not clone another
copy of the repository. For Plaits, build the audio-update file with:

```sh
make -f plaits/makefile wav
```

The generated WAV is written under `build/plaits/`.
