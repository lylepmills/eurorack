"""Placement of swappable FM bank regions, and the assertion that guards it.

A Plaits Palette build makes every FM bank replaceable over the TIMBRE input by
letting the bank's BAKED array double as the flash region a transfer erases and
reprograms. Nothing is reserved — the 4 KB a bank already occupies simply becomes
rewritable.

That only works if a region covers whole flash pages with nothing else in them.
`UserData::Save` calls `FLASH_ErasePage` on 2 KB pages, so a region sharing a
page with .text or .rodata would erase firmware on the first transfer — a bricked
module, not a degraded feature. Two things make it hold:

  * `render_linker_script` gives the banks their own page-aligned output section,
    so the linker cannot pack other data into their pages. It must be a separate
    section: putting a 2 KB alignment on arrays inside `.text` propagates to the
    whole output section and costs ~4 KB of padding after the vector table.
  * `check_regions` proves it on the LINKED ELF, which is the only artifact that
    can actually be wrong. It is a build gate, not a diagnostic.
"""

from __future__ import annotations

import re
import subprocess
from pathlib import Path

# stmlib/system/flash_programming.h, STM32F37X.
FLASH_PAGE_SIZE = 2048
# One bank: 32 packed DX7 voices, and exactly two flash pages.
REGION_SIZE = 4096

# The stock script defines _etext/_sidata at the end of .text, and .data is
# loaded from there. The bank section has to sit BEFORE that anchor, or the
# .data initializers would be stored on top of the banks.
_ANCHOR = """    . = ALIGN(16);
     _etext = .;
     _sidata = _etext;
  } >FLASH
"""

_REPLACEMENT = """    . = ALIGN(16);
  }} >FLASH

  /* The install marker (plaits/settings.cc) is an ORPHAN section in the stock
     script, which ld places by heuristic. Introducing the bank section below
     changes that heuristic — it lands at the top of FLASH and collides with
     .data's load address — so pin it explicitly.

     It must also stay OUT of the bank pages. The audio updater erases the page
     holding this word on every firmware install, and the firmware reads it to
     tell a reflash from a power cycle; if it shared a page with a bank, sending
     that bank a new patch set would erase it and the next boot would silently
     reset the user's Starting Options. */
  .plaits_install_marker :
  {{
    . = ALIGN(4);
    KEEP(*(.plaits_install_marker))
    . = ALIGN(16);
  }} >FLASH

  /* Swappable FM bank regions.

     Each bank is {region} bytes ({pages} flash pages) in its own
     .user_data_banks.<i> input section, and an audio transfer erases and
     reprograms one of them in place. They live in a dedicated OUTPUT section so
     that (a) nothing else can be packed into their pages, which UserData::Save
     erases wholesale, and (b) their {align}-byte alignment does not propagate to
     .text, which would waste a page of padding after the vector table.

     Per-bank INPUT sections, matched by wildcard rather than KEEP, so
     --gc-sections can still drop an individual bank no slot reaches. */
  .user_data_banks :
  {{
    . = ALIGN({align});
    *(.user_data_banks.*)
    . = ALIGN({align});
    _etext = .;
    _sidata = _etext;
  }} >FLASH
"""


def render_linker_script(stock_source: str) -> str:
    """The stock application linker script, plus the bank region section.

    Takes the stock text rather than reading it, because stmlib is an UPSTREAM
    submodule (pichenettes/stmlib) and must not be edited — the builder passes
    the rendered script with make's LINKER_SCRIPT instead.
    """
    if _ANCHOR not in stock_source:
        raise ValueError(
            "the stock linker script no longer ends .text with the expected "
            "_etext/_sidata anchor; re-derive render_linker_script against it "
            "rather than emitting a script that silently places .data over the "
            "bank regions")
    return stock_source.replace(
        _ANCHOR,
        _REPLACEMENT.format(
            region=REGION_SIZE,
            pages=REGION_SIZE // FLASH_PAGE_SIZE,
            align=FLASH_PAGE_SIZE,
        ),
        1,
    )


_SECTION_RE = re.compile(
    r"^\s*\d+\s+(\S+)\s+([0-9a-f]{8})\s+([0-9a-f]{8})", re.MULTILINE)
_SYMBOL_RE = re.compile(
    r"^([0-9a-f]{8})\s+([0-9a-f]{8})\s+\S\s+(\S+)$", re.MULTILINE)


def _allocated_sections(objdump: str) -> list[tuple[str, int, int]]:
    """(name, address, size) for every section that occupies flash or RAM."""
    result = []
    for match in _SECTION_RE.finditer(objdump):
        name, size_hex, address_hex = match.groups()
        size, address = int(size_hex, 16), int(address_hex, 16)
        if size and address:
            result.append((name, address, size))
    return result


def check_regions(objdump_output: str, nm_output: str, expected_count: int) -> list[str]:
    """Assert every bank region is safely erasable. Returns the region symbols.

    Raises ValueError naming the specific violation. The caller must fail the
    build on it: a region that overlaps code is not a degraded feature, it is a
    module that erases its own firmware the first time a user sends it a bank.
    """
    # Matched as a SUBSTRING: the banks are C++ objects in namespace plaits, so
    # nm reports them mangled (_ZN6plaits23kUserDataBankOverride_0E). Prefix
    # matching silently found nothing and turned this gate into a no-op.
    regions = [
        (name, int(address, 16), int(size, 16))
        for address, size, name in _SYMBOL_RE.findall(nm_output)
        if "kUserDataBankOverride_" in name
    ]
    if len(regions) != expected_count:
        raise ValueError(
            f"linked firmware carries {len(regions)} bank regions, "
            f"but the generated config declares {expected_count}")

    for name, address, size in regions:
        if size != REGION_SIZE:
            raise ValueError(
                f"{name} is {size} bytes; a region must be exactly "
                f"{REGION_SIZE} so a transfer fills whole flash pages")
        if address % FLASH_PAGE_SIZE:
            raise ValueError(
                f"{name} is at {address:#010x}, which is not a "
                f"{FLASH_PAGE_SIZE}-byte page boundary; erasing it would take "
                f"out the start of the page it shares")

    # Nothing else may live in a region's pages. The region section itself is
    # allowed to contain them; every other allocated section must stay clear.
    pages = []
    for name, address, size in regions:
        pages.append((name, address, address + size))
    for section, address, size in _allocated_sections(objdump_output):
        if section == ".user_data_banks":
            continue
        for name, start, end in pages:
            if address < end and start < address + size:
                raise ValueError(
                    f"section {section} ({address:#010x}+{size}) overlaps the "
                    f"flash pages of region {name} ({start:#010x}..{end:#010x}); "
                    f"the first transfer to that bank would erase it")
    return [name for name, _, _ in regions]


def check_elf(elf_path: Path, expected_count: int, objdump: str = "arm-none-eabi-objdump",
              nm: str = "arm-none-eabi-nm") -> list[str]:
    """check_regions against a linked ELF on disk."""
    sections = subprocess.run(
        [objdump, "-h", str(elf_path)],
        capture_output=True, text=True, check=True).stdout
    symbols = subprocess.run(
        [nm, "-S", str(elf_path)],
        capture_output=True, text=True, check=True).stdout
    return check_regions(sections, symbols, expected_count)
