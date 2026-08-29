#!/usr/bin/env python3
"""Render a recipe-specific Plaits Palette field guide as a deterministic PDF."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from generate_engine_config import validate_recipe


BUILDER_DIR = Path(__file__).resolve().parent
PUBLIC_CATALOG_PATH = BUILDER_DIR.parent / "plaits_lab_catalog/public_catalog.json"

# The lowest PLAITS_MANUAL_CONTRACT that correctly describes what this renderer
# prints. Raise it in the same commit that changes the guide's layout or prose,
# then bump the Worker's var to match when the image carrying this file ships.
#
# This is a REQUIREMENT, not the deployed value: the contract itself stays the
# Worker's alone, and the renderer is allowed to sit ahead of it while it waits
# out a rollout cycle. What is NOT allowed is deploying a Worker whose contract
# is below the requirement of the renderer inside the image it points at — that
# serves cached guides written by an older renderer under a key that no longer
# describes them. `pnpm run contract:check` enforces exactly that; see
# check_manual_contract.py and the contract notes in README.md.
MANUAL_CONTRACT = 21
BANKS = (
    {"id": "green", "name": "GREEN", "start": 0, "color": "#4F9868"},
    {"id": "red", "name": "RED", "start": 8, "color": "#C6534B"},
    {"id": "amber", "name": "AMBER", "start": 16, "color": "#D59635"},
    # The opt-in fourth bank; matches the editor's orange (plaits-palette.css).
    {"id": "orange", "name": "ORANGE", "start": 24, "color": "#D96F35"},
)
ACCESSIBLE_BANK_NAMES = ("BRIGHTEST", "BRIGHT", "DIM", "DIMMEST")
ACCESSIBLE_BANK_LEVELS = ("100%", "50%", "25%", "12.5%")
ACCESSIBLE_BANK_COLOR = "#687069"
CONTROL_IDS = ("harmonics", "timbre", "morph", "macro")
PANEL_LABELS = ("HARMONICS", "TIMBRE", "MORPH", "MACRO")
FACTORY_TERRAIN_NAMES = (
    "Asymmetric Saddle",
    "Pinched Crossing",
    "Resonant Cross",
    "Offset Folds",
    "Twin Wells",
    "Wavetable Terrain: Bank 3",
    "Wavetable Terrain: Bank 2",
    "Wavetable Terrain: Bank 1",
)
FACTORY_WAVETABLE_NAMES = {
    "mutable-1": "Harmonic Ladder",
    "mutable-2": "Pulse & Sync",
    "mutable-3": "Shape Mosaic",
}

# The options menu is eight "lights", ordered so the ones a player reaches for
# most sit nearest the start of the walk. Each light cycles through an ordered
# list of settings, and the module shows the current one as an LED color (plaits
# ui.cc): value 0 green, 1 red, 2 yellow, then 3-5 the SAME colors blinking — so
# a light can hold more than three settings.
# Within a light the settings are likewise ordered by reach, which is why the
# fourth macro and stereo sit at value 1 (solid red) rather than out in the
# blinking tier. Both orders mirror plaits/dsp/voice.h — keep them in step.
MENU_LIGHTS = (
    ("TRIG response", ("Trigger", "Gate", "Velocity trigger", "Velocity gate")),
    ("Aux output", ("Regular aux model", "Stereo (OUT/AUX = L/R)", "Suboscillator")),
    ("Suboscillator", (
        "Square", "Square, -1 octave", "Square, -2 octaves",
        "Sine", "Sine, -1 octave", "Sine, -2 octaves",
    )),
    ("FREQUENCY knob", (
        "Octaves", "MACRO (fourth control)", "Aux crossfade", "LPG decay",
        "Envelope contour",
    )),
    ("MODEL input", (
        "Model select", "MACRO (fourth control)", "Aux crossfade",
        "LPG colour (VCFA->VCA)", "Sync In (experimental)",
    )),
    ("LEVEL input", ("Level", "LPG decay", "Auto: decay or velocity")),
    ("Hold on trigger", ("Off (live CV)", "Sample & hold")),
    ("Unpatched attenuverters", (
        "Default behavior", "Continuous drift", "Triggered random step",
    )),
)

ATTENUVERTER_OPTIONS_NOTE = (
    "LIGHT 8 changes what unpatched TIMBRE and MORPH attenuverters do: Default behavior preserves the original behavior, "
    "Drift moves continuously, and Step picks a new held offset on each physical TRIG. In Drift or Step, CCW selects "
    "restrained movement close to the knob setting and CW selects broader, farther-reaching movement; both directions are "
    "bipolar. Moving farther from center increases the range, and in Drift also the speed. Patched CV and each model's "
    "dedicated attenuverter behavior always take priority. "
)

SYNC_INPUT_OPTIONS_NOTE = (
    "Sync In is experimental. Its character varies by model, and fast sync signals can produce harsh or aliased textures. "
    "Some combinations of model, parameter settings, and stereo output may exceed the module's processing headroom, causing digital distortion or dropouts. "
    "If this occurs, reduce the sync frequency, adjust the model parameters, or disable stereo output. "
)

# LED appearance for option values 0..8 (plaits ui.cc): green/red/yellow solid
# (0-2), the same three blinking (3-5), then the same three blinking fast (6-8).
# (label, hex, blink).
LED_STATES = (
    ("Green", BANKS[0]["color"], None),
    ("Red", BANKS[1]["color"], None),
    ("Yellow", BANKS[2]["color"], None),
    ("Green", BANKS[0]["color"], "blink"),
    ("Red", BANKS[1]["color"], "blink"),
    ("Yellow", BANKS[2]["color"], "blink"),
    ("Green", BANKS[0]["color"], "fast blink"),
    ("Red", BANKS[1]["color"], "fast blink"),
    ("Yellow", BANKS[2]["color"], "fast blink"),
)


def load_catalog() -> dict[str, Any]:
    return json.loads(PUBLIC_CATALOG_PATH.read_text(encoding="utf-8"))


def position(slot: int, color_blind_mode: bool = False) -> dict[str, Any]:
    bank = BANKS[slot // 8]
    bank_index = slot // 8
    result = {
        "bank": bank["id"],
        "bankName": ACCESSIBLE_BANK_NAMES[bank_index] if color_blind_mode else bank["name"],
        "color": ACCESSIBLE_BANK_COLOR if color_blind_mode else bank["color"],
        "number": slot % 8 + 1,
    }
    if color_blind_mode:
        result["brightness"] = ACCESSIBLE_BANK_LEVELS[bank_index]
    return result


def _bank_credit(bank: Any) -> dict[str, Any]:
    """The printable half of a custom FM bank: who made it and how big it is.
    The packed patch bytes are the ARM build's business; the guide only credits
    the bank and states its patch count, which is what the HARMONICS dial spans.
    Metadata is untrusted here (validate_recipe checks only the voices), so every
    field is coerced and clipped to the Worker contract's own limits."""

    def text(key: str, limit: int) -> str:
        value = bank.get(key) if isinstance(bank, dict) else None
        return value[:limit].strip() if isinstance(value, str) else ""

    voices = bank.get("voices") if isinstance(bank, dict) else None
    return {
        "name": text("name", 80),
        "author": text("author", 80),
        "origin": text("origin", 32),
        "description": text("description", 240),
        "patches": len(voices) if isinstance(voices, list) else 0,
    }


def custom_bank_credits(recipe: Any, slots: list[str | None], by_id: dict[str, Any]) -> dict[int, dict[str, Any]]:
    """Map each public slot whose built-in FM bank the recipe replaced to that
    bank's credit. Both recipe shapes are honored: v6 keys a bank by the BUILT-IN
    index it overrides (so it lands on every slot playing that factory bank),
    while v12/v13 key it by the palette slot, so two placements of the same FM
    engine can hold different banks."""
    resources = recipe.get("resources") if isinstance(recipe, dict) else None
    entries = resources.get("userDataBanks") if isinstance(resources, dict) else None
    if not isinstance(entries, list):
        return {}
    credits: dict[int, dict[str, Any]] = {}
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        credit = _bank_credit(entry.get("bank"))
        if "slot" in entry:
            slot = entry["slot"]
            if isinstance(slot, int) and 0 <= slot < len(slots) and slots[slot] is not None:
                credits[slot] = credit
            continue
        index = entry.get("index")
        if not isinstance(index, int):
            continue
        for slot, engine_id in enumerate(slots):
            if engine_id is None:
                continue
            implementation = by_id.get(engine_id, {}).get("implementation", {})
            if implementation.get("userDataBank") == index:
                credits[slot] = credit
    return credits


def custom_model_credits(recipe: Any, slots: list[str | None]) -> dict[int, dict[str, str]]:
    """Printable identity for schema-24 terrain/wavetable slot data."""
    resources = recipe.get("resources") if isinstance(recipe, dict) else None
    entries = resources.get("customModelData") if isinstance(resources, dict) else None
    if not isinstance(entries, list):
        return {}
    credits: dict[int, dict[str, str]] = {}
    for entry in entries:
        if not isinstance(entry, dict) or not isinstance(entry.get("model"), dict):
            continue
        slot = entry.get("slot")
        model = entry["model"]
        if (isinstance(slot, int) and 0 <= slot < len(slots)
                and slots[slot] in ("wave-terrain", "wavetable")):
            credits[slot] = {
                "kind": str(model.get("kind", ""))[:32],
                "name": str(model.get("name", ""))[:80].strip(),
            }
    return credits


def terrain_bank_entries(recipe: Any) -> list[dict[str, str]]:
    """The player-facing order and storage behavior of a shared terrain bank.

    validate_recipe() has already rejected malformed entries before this helper
    runs, so the remaining work is deliberately presentational: retain names and
    representation without carrying the 4 KB sample grids into the document.
    """
    resources = recipe.get("resources") if isinstance(recipe, dict) else None
    entries = resources.get("terrainBank") if isinstance(resources, dict) else None
    if not isinstance(entries, list):
        return []
    result: list[dict[str, str]] = []
    for entry in entries:
        if entry["kind"] == "factory":
            index = int(str(entry["id"]).split("-")[-1]) - 1
            result.append({
                "name": f"{FACTORY_TERRAIN_NAMES[index]} (stock bank {index + 1})",
                "storage": "Stock terrain · fixed in firmware",
            })
        else:
            model = entry["model"]
            native = model.get("representation") == "native"
            result.append({
                "name": str(model["name"])[:80].strip(),
                "storage": (
                    "Compiled equation · fixed in firmware"
                    if native else
                    "Prebaked samples · replaceable over TIMBRE"
                ),
            })
    return result


def wavetable_bank_document(recipe: Any) -> dict[str, Any]:
    """Player-facing order, transport, and storage for schema-26 Wavetable banks."""
    resources = recipe.get("resources") if isinstance(recipe, dict) else None
    bank = resources.get("wavetableBank") if isinstance(resources, dict) else None
    if not isinstance(bank, dict) or not isinstance(bank.get("entries"), list):
        return {"mirrored": True, "entries": []}
    entries: list[dict[str, str]] = []
    for entry in bank["entries"]:
        if entry["kind"] == "factory":
            factory_id = str(entry["id"])
            entries.append({
                "name": FACTORY_WAVETABLE_NAMES.get(factory_id, factory_id),
                "storage": "Stock bank · shared factory pool",
            })
        else:
            model = entry["model"]
            entries.append({
                "name": str(model["name"])[:80].strip(),
                "storage": (
                    "Compiled equation · fixed in firmware"
                    if model.get("representation") == "native" else
                    "Prebaked 64 × 128 samples · fixed in firmware"
                ),
            })
    return {"mirrored": bool(bank["mirrored"]), "entries": entries}


def manual_document(recipe: Any, build_key: str | None = None) -> dict[str, Any]:
    build = validate_recipe(recipe)
    color_blind_mode = build.color_blind_mode == 1
    slots = build.public_slots
    catalog = load_catalog()
    by_id = {engine["id"]: engine for engine in catalog["engines"]}
    fm_capabilities = catalog.get("fmCapabilities", {})
    linear_tzfm_engines = set(fm_capabilities.get("linearTzfm", ()))
    fast_fm_engines = set(fm_capabilities.get("fastFm", ()))

    def active_fm_capabilities(engine_id: str) -> dict[str, bool]:
        # Match the editor: capability badges describe what THIS firmware does,
        # so an otherwise-qualified model stays unbadged while its corresponding
        # experimental build option is off.
        return {
            "linearTzfm": build.linear_tzfm == 1 and engine_id in linear_tzfm_engines,
            "fastFm": build.fast_fm == 1 and engine_id in fast_fm_engines,
        }

    credits = custom_bank_credits(recipe, slots, by_id)
    custom_models = custom_model_credits(recipe, slots)
    models: list[dict[str, Any]] = []
    seen: set[tuple[str, str | None, str | None]] = set()
    for slot, engine_id in enumerate(slots):
        # v7 short-bank recipes leave empty slots as None — they have no model
        # reference, so they must not be dereferenced (KeyError) or listed.
        if engine_id is None:
            continue
        # Identity is the engine AND its bank: two slots of one FM engine holding
        # different custom banks are different models to a player, so they get
        # their own reference entries rather than merging into one.
        credit = credits.get(slot)
        custom_model = custom_models.get(slot)
        key = (
            engine_id,
            json.dumps(credit, sort_keys=True) if credit else None,
            json.dumps(custom_model, sort_keys=True) if custom_model else None,
        )
        if key in seen:
            continue
        seen.add(key)
        engine = by_id[engine_id]
        locations = [
            position(index, color_blind_mode)
            for index, value in enumerate(slots)
            if value == engine_id and (credits.get(index) or None) == credit
            and (custom_models.get(index) or None) == custom_model
        ]
        models.append({
            **engine,
            "locations": locations,
            "customBank": credit,
            "fmCapabilities": active_fm_capabilities(engine_id),
            "customModel": custom_model,
        })
    return {
        "buildKey": build_key,
        "target": recipe.get("target"),
        "slots": [
            {
                "engine": by_id[engine_id] if engine_id is not None else None,
                "position": position(slot, color_blind_mode),
                "customBank": credits.get(slot),
                "customModel": custom_models.get(slot),
            }
            for slot, engine_id in enumerate(slots)
        ],
        "models": models,
        "chordTables": [table["name"] for table in build.chord_tables],
        "scaleBank": (
            [scale["name"] for scale in build.scale_bank]
            if any(engine_id in (
                "diatonic-chord",
                "scale-stack",
                "wavetable-chord",
                "wavetable-scale-stack",
            ) for engine_id in slots)
            else []
        ),
        "terrainBank": terrain_bank_entries(recipe),
        "wavetableBank": wavetable_bank_document(recipe),
        # Only a build that compiled the procedure in answers the power-up
        # gesture, so only that build's guide documents it.
        "calibration": build.enable_calibration == 1,
        "colorBlindMode": color_blind_mode,
        "linearTzfm": build.linear_tzfm == 1,
        "fastFm": build.fast_fm == 1,
        "lockedFrequencyPotOption": build.locked_frequency_pot_option,
        "trigResponseOption": build.trig_response_option,
        "envelopeContour": build.envelope_contour == 1,
        "modelCVOption": build.model_cv_option,
    }


def _escape(value: str) -> str:
    return value.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


# Once a six-op FM slot carries a custom bank, the factory A/B/C letter is
# meaningless to the player — the slot no longer plays those patches. So the
# guide names it the way the editor does: "Custom 6-Op FM Bank", with the bank's
# own name as the subtitle wherever there is room for one.
CUSTOM_BANK_LABEL = "Custom 6-Op FM Bank"


def display_name(
        engine_name: str,
        credit: dict[str, Any] | None,
        custom_model: dict[str, str] | None = None) -> str:
    if credit:
        return CUSTOM_BANK_LABEL
    if custom_model:
        return "Custom Wave Terrain" if custom_model["kind"] == "wave-terrain" else "Custom Wavetable"
    return engine_name


def _clip(value: str, font: str, size: float, max_width: float) -> str:
    """Trim a string to one line at the given font/size, ellipsizing if it can't
    fit. The bank map's slot rows are a fixed height, so a long bank name has to
    lose its tail rather than wrap into the row below."""
    from reportlab.pdfbase.pdfmetrics import stringWidth

    if stringWidth(value, font, size) <= max_width:
        return value
    clipped = value
    while clipped and stringWidth(clipped + "…", font, size) > max_width:
        clipped = clipped[:-1]
    return (clipped.rstrip() + "…") if clipped else ""


def render_pdf(document: dict[str, Any], output: Path) -> None:
    from reportlab.lib import colors
    from reportlab.lib.enums import TA_CENTER, TA_LEFT
    from reportlab.lib.pagesizes import letter
    from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
    from reportlab.lib.units import inch
    from reportlab.pdfgen.canvas import Canvas
    from reportlab.platypus import (
        PageBreak,
        Paragraph,
        SimpleDocTemplate,
        Spacer,
        Table,
        TableStyle,
    )

    output.parent.mkdir(parents=True, exist_ok=True)
    roved = document.get("target") == "plum-audio-roved"
    page_width, page_height = letter
    margin = 0.56 * inch
    styles = getSampleStyleSheet()
    ink = colors.HexColor("#242722")
    muted = colors.HexColor("#687069")
    paper = colors.HexColor("#FAF8F2")
    line = colors.HexColor("#D8D8CF")
    title_style = ParagraphStyle(
        "Title",
        parent=styles["Title"],
        fontName="Helvetica-Bold",
        fontSize=27,
        leading=30,
        textColor=ink,
        alignment=TA_LEFT,
        spaceAfter=8,
    )
    kicker_style = ParagraphStyle(
        "Kicker",
        parent=styles["Normal"],
        fontName="Helvetica-Bold",
        fontSize=8,
        leading=10,
        textColor=colors.HexColor("#8C4F36"),
        tracking=1.8,
        spaceAfter=5,
    )
    intro_style = ParagraphStyle(
        "Intro",
        parent=styles["BodyText"],
        fontName="Helvetica",
        fontSize=10,
        leading=14,
        textColor=muted,
        spaceAfter=12,
    )
    section_style = ParagraphStyle(
        "Section",
        parent=styles["Heading2"],
        fontName="Helvetica-Bold",
        fontSize=17,
        leading=20,
        textColor=ink,
        spaceBefore=3,
        spaceAfter=8,
    )
    model_style = ParagraphStyle(
        "Model",
        parent=styles["Heading3"],
        fontName="Helvetica-Bold",
        fontSize=14,
        leading=16,
        textColor=ink,
    )
    location_style = ParagraphStyle(
        "Location",
        parent=styles["Normal"],
        fontName="Helvetica-Bold",
        fontSize=7.5,
        leading=9,
        textColor=muted,
        alignment=TA_LEFT,
    )
    body_style = ParagraphStyle(
        "Body",
        parent=styles["BodyText"],
        fontName="Helvetica",
        fontSize=8.5,
        leading=11.2,
        textColor=ink,
    )
    small_style = ParagraphStyle(
        "Small",
        parent=styles["BodyText"],
        fontName="Helvetica",
        fontSize=7.4,
        leading=9.4,
        textColor=ink,
    )
    small_muted_style = ParagraphStyle(
        "SmallMuted",
        parent=small_style,
        textColor=muted,
    )
    table_header_style = ParagraphStyle(
        "TableHeader",
        parent=styles["Normal"],
        fontName="Helvetica-Bold",
        fontSize=6.4,
        leading=7.5,
        textColor=muted,
    )
    bank_name_style = ParagraphStyle(
        "BankName",
        parent=styles["Normal"],
        fontName="Helvetica-Bold",
        fontSize=8,
        leading=10,
        textColor=colors.white,
        alignment=TA_CENTER,
    )
    bank_model_style = ParagraphStyle(
        "BankModel",
        parent=styles["Normal"],
        fontName="Helvetica-Bold",
        fontSize=7.7,
        leading=9.2,
        textColor=ink,
    )
    fm_badge_style = ParagraphStyle(
        "FmBadge",
        parent=styles["Normal"],
        fontName="Helvetica-Bold",
        fontSize=6.1,
        leading=7.2,
        alignment=TA_CENTER,
    )

    def fm_badges(capabilities: dict[str, bool] | None) -> Any:
        """Render the same active-only TZ / 50k tokens used by the editor."""
        specs = []
        if capabilities and capabilities.get("linearTzfm"):
            specs.append(("TZ", "#6546A8", "#EEE9FA", 0.26 * inch))
        if capabilities and capabilities.get("fastFm"):
            specs.append(("50k", "#116C65", "#E4F4F1", 0.33 * inch))
        if not specs:
            return ""
        cells: list[Any] = []
        widths: list[float] = []
        badge_columns: list[int] = []
        for index, (label, foreground, _background, width) in enumerate(specs):
            if index:
                cells.append("")
                widths.append(0.04 * inch)
            badge_columns.append(len(cells))
            cells.append(Paragraph(f'<font color="{foreground}">{label}</font>', fm_badge_style))
            widths.append(width)
        badge_table = Table([cells], colWidths=widths, rowHeights=[0.17 * inch], hAlign="LEFT")
        badge_style = [
            ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
            ("LEFTPADDING", (0, 0), (-1, -1), 0),
            ("RIGHTPADDING", (0, 0), (-1, -1), 0),
            ("TOPPADDING", (0, 0), (-1, -1), 0),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 0),
        ]
        for column, (_label, foreground, background, _width) in zip(badge_columns, specs):
            badge_style.extend([
                ("BACKGROUND", (column, 0), (column, 0), colors.HexColor(background)),
                ("BOX", (column, 0), (column, 0), 0.45, colors.HexColor(foreground)),
            ])
        badge_table.setStyle(TableStyle(badge_style))
        return badge_table

    def footer(canvas: Canvas, doc: SimpleDocTemplate) -> None:
        canvas.saveState()
        canvas.setStrokeColor(line)
        canvas.setLineWidth(0.4)
        canvas.line(margin, 0.41 * inch, page_width - margin, 0.41 * inch)
        canvas.setFillColor(muted)
        canvas.setFont("Helvetica", 6.8)
        canvas.drawString(margin, 0.25 * inch, "PLAITS PALETTE")
        canvas.drawRightString(page_width - margin, 0.25 * inch, str(doc.page))
        canvas.restoreState()

    bank_count = len(document["slots"]) // 8
    if document.get("colorBlindMode"):
        bank_phrase = "three-bank brightness" if bank_count == 3 else "four-bank brightness"
    else:
        bank_phrase = (
            "green, red, and amber"
            if bank_count == 3
            else "green, red, amber, and orange"
        )
    story: list[Any] = [
        Paragraph("RUBATO AUDIO  /  PLAITS PALETTE", kicker_style),
        Paragraph("Your Ro'Ved Field Guide" if roved else "Your Plaits Field Guide", title_style),
        Paragraph(
            f"A rack-side reference generated from the exact {bank_phrase} layout in this firmware recipe. "
            f"This guide contains {len(document['models'])} unique synthesis models.",
            intro_style,
        ),
        Paragraph("Bank map", section_style),
    ]

    # A customized six-op slot prints its bank's name under the label, which
    # needs a second line. Rows stay a uniform height across all three (or four)
    # bank tables so slot N of one bank still lines up with slot N of the next.
    subtitled = any(
        entry["engine"] is not None and (
            (entry["customBank"] or {}).get("name")
            or (entry["customModel"] or {}).get("name")
        )
        for entry in document["slots"]
    )
    slot_row_height = (0.35 if subtitled else 0.31) * inch

    bank_tables = []
    for bank_index, bank in enumerate(BANKS[:bank_count]):
        display_position = document["slots"][bank_index * 8]["position"]
        display_bank_name = display_position["bankName"]
        if display_position.get("brightness"):
            display_bank_name += f"  {display_position['brightness']}"
        display_color = display_position["color"]
        rows: list[list[Any]] = [[Paragraph(display_bank_name, bank_name_style)]]
        for bank_slot in range(8):
            entry = document["slots"][bank_index * 8 + bank_slot]
            engine = entry["engine"]
            credit = entry["customBank"]
            custom_model = entry["customModel"]
            # Empty slots (v7 short banks or a v11 sparse-bank gap) have no
            # engine — show a muted dash at the slot's true position.
            if engine is None:
                name_para = Paragraph("—", small_muted_style)
            else:
                label = _escape(display_name(engine["name"], credit, custom_model))
                subtitle = _escape(_clip(
                    (credit or custom_model or {}).get("name", ""),
                    "Helvetica", 6.4, 1.55 * inch,
                ))
                name_para = Paragraph(
                    f'{label}<br/><font face="Helvetica" size="6.4" color="#687069">{subtitle}</font>'
                    if (credit or custom_model) and subtitle
                    else label,
                    bank_model_style,
                )
            rows.append([
                Table(
                    [[Paragraph(f"{bank_slot + 1:02d}", small_muted_style), name_para]],
                    colWidths=[0.25 * inch, 1.72 * inch],
                    style=TableStyle([
                        ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
                        ("LEFTPADDING", (0, 0), (-1, -1), 0),
                        ("RIGHTPADDING", (0, 0), (-1, -1), 0),
                        ("TOPPADDING", (0, 0), (-1, -1), 0),
                        ("BOTTOMPADDING", (0, 0), (-1, -1), 0),
                    ]),
                )
            ])
        bank_table = Table(rows, colWidths=[2.05 * inch], rowHeights=[0.26 * inch] + [slot_row_height] * 8)
        bank_table.setStyle(TableStyle([
            ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor(display_color)),
            ("BACKGROUND", (0, 1), (-1, -1), colors.white),
            ("BOX", (0, 0), (-1, -1), 0.6, colors.HexColor(display_color)),
            ("INNERGRID", (0, 1), (-1, -1), 0.35, line),
            ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
            ("LEFTPADDING", (0, 0), (-1, -1), 6),
            ("RIGHTPADDING", (0, 0), (-1, -1), 6),
            ("TOPPADDING", (0, 0), (-1, -1), 3),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
        ]))
        bank_tables.append(bank_table)
    # Three banks sit in one row; a fourth-bank recipe wraps to a 2x2 grid so
    # the columns keep their legible width on the Letter page.
    per_row = 3 if len(bank_tables) <= 3 else 2
    bank_map_rows = [bank_tables[i : i + per_row] for i in range(0, len(bank_tables), per_row)]
    bank_map_rows[-1] += [""] * (per_row - len(bank_map_rows[-1]))
    bank_map = Table(bank_map_rows, colWidths=[2.12 * inch] * per_row, hAlign="LEFT")
    bank_map_style = [
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 0),
        ("RIGHTPADDING", (0, 0), (-1, -1), 5),
        ("TOPPADDING", (0, 0), (-1, -1), 0),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 0),
    ]
    if len(bank_map_rows) > 1:
        bank_map_style.append(("BOTTOMPADDING", (0, 0), (-1, -2), 8))
    bank_map.setStyle(TableStyle(bank_map_style))

    # Options menu — the eight-light reference. Each light's settings are shown in LED order, with
    # the color word tinted to the LED and "(blink)"/"(fast blink)" for the
    # blinking values.
    def led_setting(index: int, meaning: str) -> str:
        if document.get("colorBlindMode"):
            brightness = ("Brightest (100%)", "Medium (50%)", "Dim (25%)")[index % 3]
            blink_tier = index // 3
            blink = (None, "slow blink", "fast blink")[blink_tier]
            state = f"{brightness}, {blink}" if blink else brightness
            return f"<b>{state}</b>: {_escape(meaning)}"
        label, hex_color, blink = LED_STATES[index]
        state = f"{label} ({blink})" if blink else label
        return f'<font color="{hex_color}"><b>{state}</b></font>: {_escape(meaning)}'

    menu_rows: list[list[Any]] = [[
        Paragraph("LIGHT", table_header_style),
        Paragraph("ASSIGNS", table_header_style),
        Paragraph("SETTINGS (BY LED)", table_header_style),
    ]]
    for light_index, (assigns, meanings) in enumerate(MENU_LIGHTS):
        if light_index == 3 and not document.get("envelopeContour"):
            # The contour code is recipe-scoped for flash.
            meanings = meanings[:4]
        elif light_index == 4 and document.get("modelCVOption", 0) < 4:
            # Sync detection and engine reset paths are recipe-scoped for flash.
            # Guides for ordinary builds keep the original four settings.
            meanings = meanings[:4]
        lines = [led_setting(k, meaning) for k, meaning in enumerate(meanings)]
        menu_rows.append([
            Paragraph(str(light_index + 1), small_muted_style),
            Paragraph(_escape(assigns), small_style),
            Paragraph("<br/>".join(lines), small_style),
        ])
    menu_table = Table(
        menu_rows,
        colWidths=[0.45 * inch, 1.5 * inch, 4.35 * inch],
        repeatRows=1,
    )
    menu_table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#EFECE3")),
        ("GRID", (0, 0), (-1, -1), 0.35, line),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 5),
        ("RIGHTPADDING", (0, 0), (-1, -1), 5),
        ("TOPPADDING", (0, 0), (-1, -1), 4),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
    ]))

    color_blind_mode = document.get("colorBlindMode")
    if color_blind_mode:
        fourth_control = (
            "Press the HARMONICS knob down, keep holding it, and turn until the model LEDs blink; this selects the octave-switching frequency range. "
            "Click TIMBRE + FREQUENCY together to open the alternate-firmware options menu. Click TIMBRE to walk forward to LIGHT 4, then click HARMONICS once, until it uses medium brightness. "
            "Click TIMBRE + FREQUENCY again to exit. The FREQUENCY knob now controls the selected model's MACRO, its fourth synthesis control; for Mutable Instruments models, noon preserves the original sound."
            if roved else
            "Hold the right button and turn HARMONICS until the model LEDs blink; this selects the octave-switching frequency range. "
            "Short-press both model buttons to open the alternate-firmware options menu. Use the left button to walk to LIGHT 4, then press the right button once, until it uses medium brightness. "
            "Press both buttons again to exit. The FREQUENCY knob now controls the selected model's MACRO, its fourth synthesis control; for Mutable Instruments models, noon preserves the original sound."
        )
        options_intro = (
            "Click TIMBRE + FREQUENCY together to enter or exit the options menu. All eight lights are the menu: "
            "click FREQUENCY/TIMBRE for the previous/next light, and MORPH/HARMONICS for the previous/next setting. "
            "The first three settings use brightest, medium, and dim; settings four through six repeat those levels with a slow blink."
            if roved else
            "Short-press both buttons at once to enter or exit the options menu. All eight lights are the menu: "
            "the left button moves between them, and the right button steps through a light's settings. "
            "The first three settings use brightest, medium, and dim; settings four through six repeat those levels with a slow blink."
        )
    else:
        fourth_control = (
            "Press the HARMONICS knob down, keep holding it, and turn until the model LEDs blink yellow; this selects the octave-switching frequency range. "
            "Click TIMBRE + FREQUENCY together to open the alternate-firmware options menu. Click TIMBRE to walk forward to LIGHT 4, then click HARMONICS once, until it turns red. "
            "Click TIMBRE + FREQUENCY again to exit. The FREQUENCY knob now controls the selected model's MACRO, its fourth synthesis control; for Mutable Instruments models, noon preserves the original sound."
            if roved else
            "Hold the right button and turn HARMONICS until the model LEDs blink yellow; this selects the octave-switching frequency range. "
            "Short-press both model buttons to open the alternate-firmware options menu. Use the left button to walk to LIGHT 4, then press the right button once, until it turns red. "
            "Press both buttons again to exit. The FREQUENCY knob now controls the selected model's MACRO, its fourth synthesis control; for Mutable Instruments models, noon preserves the original sound."
        )
        options_intro = (
            "Click TIMBRE + FREQUENCY together to enter or exit the options menu. All eight lights are the menu: "
            "click FREQUENCY/TIMBRE for the previous/next light, and MORPH/HARMONICS for the previous/next setting. "
            "The light's color shows the current setting — green, red, and yellow, then the same three colors blinking for a fourth, fifth, or sixth setting."
            if roved else
            "Short-press both buttons at once to enter or exit the options menu. All eight lights are the menu: "
            "the left button moves between them, the right button steps through a light's settings, and the light's color shows the current one — "
            "green, red, and yellow, then the same three colors blinking for a fourth, fifth, or sixth setting."
        )
    contour_options_note = (
        "LIGHT 4's Elements-derived Envelope contour uses FREQUENCY for timing. "
        "LIGHT 1 makes it a one-shot attack/decay in either Trigger mode, or an "
        "attack/sustain/release shape in either Gate mode; either Velocity "
        "mode scales the contour from the rising-edge voltage. "
        if document.get("envelopeContour") else ""
    )
    sync_input_options_note = (
        SYNC_INPUT_OPTIONS_NOTE
        if document.get("modelCVOption", 0) == 4 else ""
    )
    options_note = (
        "LIGHT 1 chooses Trigger, Gate, Velocity trigger, or Velocity gate response. "
        "LIGHT 3 stays dark, and the light navigation skips it, unless LIGHT 2 is set to a suboscillator — it has nothing to act on otherwise. "
        "LIGHT 4 applies in octave-switching (frequency-locked) mode. Whenever LIGHT 4 is not Octaves, push and hold FREQUENCY and turn it to change octaves. "
        "LIGHT 6's LPG-decay and Auto settings apply only when TRIG is patched. "
        f"{contour_options_note}"
        f"{sync_input_options_note}"
        "Auto sends LEVEL to LPG decay on ordinary oscillator models, but keeps LEVEL as velocity/accent on models with their own envelope. "
        f"{ATTENUVERTER_OPTIONS_NOTE}"
        "Outside the menu, click FREQUENCY/TIMBRE for previous/next model and "
        "MORPH/HARMONICS for previous/next bank. Model clicks stay inside the current bank."
        if roved else
        "LIGHT 1 chooses Trigger, Gate, Velocity trigger, or Velocity gate response. "
        "LIGHT 3 stays dark, and the left button walks past it, unless LIGHT 2 is set to a suboscillator — it has nothing to act on otherwise. "
        "LIGHT 4 applies in octave-switching (frequency-locked) mode. Whenever LIGHT 4 is not Octaves, hold the right button and turn MORPH to change octaves. "
        f"{contour_options_note}"
        f"{sync_input_options_note}"
        "LIGHT 6's LPG-decay and Auto settings apply only when TRIG is patched. "
        "Auto sends LEVEL to LPG decay on ordinary oscillator models, but keeps LEVEL as velocity/accent on models with their own envelope. "
        f"{ATTENUVERTER_OPTIONS_NOTE}"
        "Model navigation (linear or banked) is chosen when you build the firmware, not from this menu."
    )
    chord_gesture = (
        "Push and turn HARMONICS while a chord-table model is active."
        if roved else
        "Hold the left model button and turn HARMONICS while a chord-table model is active."
    )
    chord_table_lines = []
    for index, name in enumerate(document["chordTables"]):
        bank = "A · light on" if index < 8 else "B · light off"
        chord_table_lines.append(
            f"{index + 1}. {_escape(name)} ({bank} {(index % 8) + 1})")
    chord_tables_note = (
        f"{chord_gesture} Tables 1–8 show one yellow light; tables 9–16 show "
        "seven yellow lights with the selected position dark.<br/>" +
        "<br/>".join(chord_table_lines)
    )
    fine_tuning = (
        "Press and hold HARMONICS, then turn it to choose the frequency range. Fine tuning is the position after octave switching and before the high-frequency range; all eight model lights pulse together. "
        "Release HARMONICS, then turn FREQUENCY to tune one semitone above or below the current manual pitch. The pitch starts where it was, responds as soon as you turn, and saves automatically two seconds after you stop. "
        "That saved pitch becomes the root used by octave switching and remains after power cycles. Patched V/OCT and FM are not folded into the saved tuning."
        if roved else
        "Hold the right button and turn HARMONICS to choose the frequency range. Fine tuning is the position after octave switching and before the high-frequency range; all eight model lights pulse together. "
        "Release the button, then turn FREQUENCY to tune one semitone above or below the current manual pitch. The pitch starts where it was, responds as soon as you turn, and saves automatically two seconds after you stop. "
        "That saved pitch becomes the root used by octave switching and remains after power cycles. Patched V/OCT and FM are not folded into the saved tuning."
    )
    linear_tzfm = bool(document.get("linearTzfm"))
    fast_fm = bool(document.get("fastFm"))
    if linear_tzfm and fast_fm:
        experimental_fm_note = (
            "Models carrying both badges combine 50 kHz linear through-zero FM counter-clockwise with 50 kHz exponential FM clockwise. "
            "A TZ-only model uses both laws at the normal 4 kHz control rate; a 50k-only model uses 50 kHz exponential FM in both directions. "
            "Untagged models retain normal 4 kHz exponential FM. Fast FM dedicates the shared converter to FM, so LEVEL CV is unavailable throughout this firmware, including on models without a 50k badge."
        )
    elif linear_tzfm:
        experimental_fm_note = (
            "Models carrying TZ use linear through-zero FM counter-clockwise and regular exponential FM clockwise; the center is off. "
            "Untagged models retain normal 4 kHz exponential FM. LEVEL CV remains available."
        )
    elif fast_fm:
        experimental_fm_note = (
            "Models carrying 50k digitize exponential FM continuously at 50 kHz and keep the normal bipolar attenuverter response. "
            "Untagged models retain normal 4 kHz exponential FM. Fast FM dedicates the shared converter to FM, so LEVEL CV is unavailable throughout this firmware, including on models without a 50k badge."
        )
    else:
        experimental_fm_note = ""

    story.append(bank_map)
    if color_blind_mode:
        story.extend([
            Spacer(1, 0.12 * inch),
            Table(
                [[
                    Paragraph("BANK LIGHTS", table_header_style),
                    Paragraph(
                        "This build uses the color-blind display: every model light uses one hue, "
                        "and brightness identifies the bank. From the first bank to the optional "
                        "fourth: BRIGHTEST is 100%, BRIGHT is 50%, DIM is 25%, and DIMMEST is 12.5%. "
                        "The options menu uses brightest, medium, and dim within each blink tier. "
                        "The setting is built into this firmware; no power-up gesture is required.",
                        small_style,
                    ),
                ]],
                colWidths=[1.1 * inch, 5.2 * inch],
                style=TableStyle([
                    ("BACKGROUND", (0, 0), (-1, -1), colors.HexColor("#EFECE3")),
                    ("BOX", (0, 0), (-1, -1), 0.5, line),
                    ("VALIGN", (0, 0), (-1, -1), "TOP"),
                    ("LEFTPADDING", (0, 0), (-1, -1), 8),
                    ("RIGHTPADDING", (0, 0), (-1, -1), 8),
                    ("TOPPADDING", (0, 0), (-1, -1), 7),
                    ("BOTTOMPADDING", (0, 0), (-1, -1), 7),
                ]),
            ),
        ])
    story.extend([
        Spacer(1, 0.18 * inch),
        Table(
            [[
                Paragraph("MACRO", table_header_style),
                Paragraph(fourth_control, small_style),
            ]],
            colWidths=[1.1 * inch, 5.2 * inch],
            style=TableStyle([
                ("BACKGROUND", (0, 0), (-1, -1), colors.HexColor("#EFECE3")),
                ("BOX", (0, 0), (-1, -1), 0.5, line),
                ("VALIGN", (0, 0), (-1, -1), "TOP"),
                ("LEFTPADDING", (0, 0), (-1, -1), 8),
                ("RIGHTPADDING", (0, 0), (-1, -1), 8),
                ("TOPPADDING", (0, 0), (-1, -1), 7),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 7),
            ]),
        ),
    ])
    if experimental_fm_note:
        fm_legend_rows = []
        if linear_tzfm:
            fm_legend_rows.append([
                fm_badges({"linearTzfm": True, "fastFm": False}),
                Paragraph("<b>Linear TZFM</b> — counter-clockwise through-zero FM; clockwise exponential FM.", small_style),
            ])
        if fast_fm:
            fm_legend_rows.append([
                fm_badges({"linearTzfm": False, "fastFm": True}),
                Paragraph("<b>Fast FM</b> — FM sampled at 50 kHz instead of the normal 4 kHz.", small_style),
            ])
        fm_legend = Table(fm_legend_rows, colWidths=[0.72 * inch, 4.35 * inch])
        fm_legend.setStyle(TableStyle([
            ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
            ("LEFTPADDING", (0, 0), (-1, -1), 0),
            ("RIGHTPADDING", (0, 0), (-1, -1), 4),
            ("TOPPADDING", (0, 0), (-1, -1), 1),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
        ]))
        story.extend([
            Spacer(1, 0.12 * inch),
            Table(
                [[
                    Paragraph("EXPERIMENTAL FM", table_header_style),
                    [fm_legend, Spacer(1, 0.04 * inch), Paragraph(experimental_fm_note, small_style)],
                ]],
                colWidths=[1.1 * inch, 5.2 * inch],
                style=TableStyle([
                    ("BACKGROUND", (0, 0), (-1, -1), colors.HexColor("#EFECE3")),
                    ("BOX", (0, 0), (-1, -1), 0.5, line),
                    ("VALIGN", (0, 0), (-1, -1), "TOP"),
                    ("LEFTPADDING", (0, 0), (-1, -1), 8),
                    ("RIGHTPADDING", (0, 0), (-1, -1), 8),
                    ("TOPPADDING", (0, 0), (-1, -1), 7),
                    ("BOTTOMPADDING", (0, 0), (-1, -1), 7),
                ]),
            ),
        ])
    story.extend([
        Spacer(1, 0.12 * inch),
        Table(
            [[
                Paragraph("FINE TUNING", table_header_style),
                Paragraph(fine_tuning, small_style),
            ]],
            colWidths=[1.1 * inch, 5.2 * inch],
            style=TableStyle([
                ("BACKGROUND", (0, 0), (-1, -1), colors.HexColor("#EFECE3")),
                ("BOX", (0, 0), (-1, -1), 0.5, line),
                ("VALIGN", (0, 0), (-1, -1), "TOP"),
                ("LEFTPADDING", (0, 0), (-1, -1), 8),
                ("RIGHTPADDING", (0, 0), (-1, -1), 8),
                ("TOPPADDING", (0, 0), (-1, -1), 7),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 7),
            ]),
        ),
    ])
    if document.get("lockedFrequencyPotOption") != 0:
        story.extend([
            Spacer(1, 0.12 * inch),
            Table(
                [[
                    Paragraph("LOCKED OCTAVES", table_header_style),
                    Paragraph(
                        # Ro'Ved rides the gesture on FREQUENCY's own click,
                        # free precisely because a locked FREQUENCY is no longer
                        # setting pitch. Stock Plaits has no spare knob click, so
                        # it spends its right button and MORPH instead.
                        "This build gives FREQUENCY another job, but octave switching is still available. "
                        "Push and hold FREQUENCY and turn it. The lights fill from the left as the octave rises; "
                        "all eight lights means the highest octave."
                        if roved else
                        "This build gives FREQUENCY another job, but octave switching is still available. "
                        "Hold the right button and turn MORPH. The lights climb with the selected octave; "
                        "all eight lights means the highest octave.",
                        small_style,
                    ),
                ]],
                colWidths=[1.1 * inch, 5.2 * inch],
                style=TableStyle([
                    ("BACKGROUND", (0, 0), (-1, -1), colors.HexColor("#EFECE3")),
                    ("BOX", (0, 0), (-1, -1), 0.5, line),
                    ("VALIGN", (0, 0), (-1, -1), "TOP"),
                    ("LEFTPADDING", (0, 0), (-1, -1), 8),
                    ("RIGHTPADDING", (0, 0), (-1, -1), 8),
                    ("TOPPADDING", (0, 0), (-1, -1), 7),
                    ("BOTTOMPADDING", (0, 0), (-1, -1), 7),
                ]),
            ),
        ])
    story.extend([
        PageBreak(),
        Paragraph("Options menu", section_style),
        Paragraph(options_intro, intro_style),
        menu_table,
        Spacer(1, 0.1 * inch),
        Paragraph(options_note, small_muted_style),
        Spacer(1, 0.1 * inch),
        Table(
            [[
                Paragraph("CHORD TABLES", table_header_style),
                Paragraph(chord_tables_note, small_style),
            ]],
            colWidths=[1.1 * inch, 5.2 * inch],
            style=TableStyle([
                ("BACKGROUND", (0, 0), (-1, -1), colors.HexColor("#EFECE3")),
                ("BOX", (0, 0), (-1, -1), 0.5, line),
                ("VALIGN", (0, 0), (-1, -1), "TOP"),
                ("LEFTPADDING", (0, 0), (-1, -1), 8),
                ("RIGHTPADDING", (0, 0), (-1, -1), 8),
                ("TOPPADDING", (0, 0), (-1, -1), 7),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 7),
            ]),
        ),
    ])

    if document["scaleBank"]:
        scale_rows: list[list[Any]] = [[
            Paragraph("MACRO", table_header_style),
            Paragraph("SCALE", table_header_style),
        ]]
        for index, name in enumerate(document["scaleBank"], start=1):
            scale_rows.append([
                Paragraph(str(index), small_muted_style),
                Paragraph(_escape(name), small_style),
            ])
        scale_table = Table(
            scale_rows,
            colWidths=[0.7 * inch, 5.6 * inch],
            repeatRows=1,
        )
        scale_table.setStyle(TableStyle([
            ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#EFECE3")),
            ("GRID", (0, 0), (-1, -1), 0.35, line),
            ("VALIGN", (0, 0), (-1, -1), "TOP"),
            ("LEFTPADDING", (0, 0), (-1, -1), 5),
            ("RIGHTPADDING", (0, 0), (-1, -1), 5),
            ("TOPPADDING", (0, 0), (-1, -1), 4),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
        ]))
        story.extend([
            PageBreak(),
            Paragraph("Scale bank", section_style),
            Paragraph(
                "Diatonic Chord and Scale Stack use the same scale bank. "
                "Turn MACRO from left to right to move through these scales in order.",
                intro_style,
            ),
            scale_table,
        ])

    if document["terrainBank"]:
        terrain_rows: list[list[Any]] = [[
            Paragraph("HARMONICS", table_header_style),
            Paragraph("TERRAIN", table_header_style),
            Paragraph("STORAGE", table_header_style),
        ]]
        for index, entry in enumerate(document["terrainBank"], start=1):
            terrain_rows.append([
                Paragraph(str(index), small_muted_style),
                Paragraph(_escape(entry["name"]), small_style),
                Paragraph(_escape(entry["storage"]), small_muted_style),
            ])
        terrain_table = Table(
            terrain_rows,
            colWidths=[0.7 * inch, 2.65 * inch, 2.95 * inch],
            rowHeights=[0.26 * inch] + [0.4 * inch] * len(document["terrainBank"]),
            repeatRows=1,
        )
        terrain_table.setStyle(TableStyle([
            ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#EFECE3")),
            ("GRID", (0, 0), (-1, -1), 0.35, line),
            ("VALIGN", (0, 0), (-1, -1), "TOP"),
            ("LEFTPADDING", (0, 0), (-1, -1), 5),
            ("RIGHTPADDING", (0, 0), (-1, -1), 5),
            ("TOPPADDING", (0, 0), (-1, -1), 4),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
        ]))
        story.extend([
            PageBreak(),
            Paragraph("Wave Terrain bank", section_style),
            Paragraph(
                "HARMONICS sweeps this list from top to bottom and interpolates between adjacent entries. "
                "A TIMBRE audio transfer replaces the selected prebaked custom entry. Stock and compiled "
                "equation entries refuse transfers without erasing anything.",
                intro_style,
            ),
            terrain_table,
        ])

    wavetable_bank = document["wavetableBank"]
    if wavetable_bank["entries"]:
        wavetable_rows: list[list[Any]] = [[
            Paragraph("HARMONICS", table_header_style),
            Paragraph("WAVETABLE", table_header_style),
            Paragraph("STORAGE", table_header_style),
        ]]
        for index, entry in enumerate(wavetable_bank["entries"], start=1):
            wavetable_rows.append([
                Paragraph(str(index), small_muted_style),
                Paragraph(_escape(entry["name"]), small_style),
                Paragraph(_escape(entry["storage"]), small_muted_style),
            ])
        wavetable_table = Table(
            wavetable_rows,
            colWidths=[0.7 * inch, 2.65 * inch, 2.95 * inch],
            rowHeights=[0.26 * inch] + [0.4 * inch] * len(wavetable_bank["entries"]),
            repeatRows=1,
        )
        wavetable_table.setStyle(TableStyle([
            ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#EFECE3")),
            ("GRID", (0, 0), (-1, -1), 0.35, line),
            ("VALIGN", (0, 0), (-1, -1), "TOP"),
            ("LEFTPADDING", (0, 0), (-1, -1), 5),
            ("RIGHTPADDING", (0, 0), (-1, -1), 5),
            ("TOPPADDING", (0, 0), (-1, -1), 4),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
        ]))
        transport = (
            "sweeps from top to bottom, then mirrors the same banks back to the start"
            if wavetable_bank["mirrored"] else
            "sweeps once from the first bank to the last"
        )
        story.extend([
            PageBreak(),
            Paragraph("Wavetable bank", section_style),
            Paragraph(
                f"HARMONICS {transport}, interpolating between adjacent banks. "
                "TIMBRE and MORPH move through each bank's 8 × 8 wave field; MACRO phase-warps the result.",
                intro_style,
            ),
            wavetable_table,
        ])

    # Calibration, for the builds that asked for it. Most firmwares leave the
    # procedure out — a module keeps the pitch-CV calibration it already has
    # through any firmware install, so it is only needed by a module that has
    # never been calibrated or was erased by a programmer — and a guide that
    # described a gesture the firmware does not answer would be worse than no
    # page at all.
    if document.get("calibration"):
        if color_blind_mode:
            calibration_instructions = (
                "This build includes the pitch-CV calibration procedure. "
                "Hold the MORPH knob down while powering the module up to start it: the first light pulses at the brightest level. "
                "Patch 1V into V/OCT and click any knob — the light becomes dim. "
                "Patch 3V and click any knob again. "
                "The lights return to normal and the new calibration is saved. "
                "If the two voltages are not two octaves apart, every light flashes and nothing is written, "
                "so a mis-patched attempt leaves your module exactly as it was; use any click and try again. "
                "Powering the module off part-way through also changes nothing."
                if roved else
                "This build includes the pitch-CV calibration procedure. "
                "Hold the RIGHT button while powering the module up to start it: the first light pulses at the brightest level. "
                "Patch 1V into V/OCT and press either button — the light becomes dim. "
                "Patch 3V and press either button again. "
                "The lights return to normal and the new calibration is saved. "
                "If the two voltages are not two octaves apart, every light flashes and nothing is written, "
                "so a mis-patched attempt leaves your module exactly as it was; press a button and try again. "
                "Powering the module off part-way through also changes nothing."
            )
        else:
            calibration_instructions = (
                "This build includes the pitch-CV calibration procedure. "
                "Hold the MORPH knob down while powering the module up to start it: the first light pulses green. "
                "Patch 1V into V/OCT and click any knob — the light turns yellow. "
                "Patch 3V and click any knob again. "
                "The lights return to normal and the new calibration is saved. "
                "If the two voltages are not two octaves apart, every light flashes red and nothing is written, "
                "so a mis-patched attempt leaves your module exactly as it was; use any click and try again. "
                "Powering the module off part-way through also changes nothing."
                if roved else
                "This build includes the pitch-CV calibration procedure. "
                "Hold the RIGHT button while powering the module up to start it: the first light pulses green. "
                "Patch 1V into V/OCT and press either button — the light turns yellow. "
                "Patch 3V and press either button again. "
                "The lights return to normal and the new calibration is saved. "
                "If the two voltages are not two octaves apart, every light flashes red and nothing is written, "
                "so a mis-patched attempt leaves your module exactly as it was; press a button and try again. "
                "Powering the module off part-way through also changes nothing."
            )
        story.extend([
            Spacer(1, 0.18 * inch),
            Table(
                [[
                    Paragraph("CALIBRATION", table_header_style),
                    Paragraph(calibration_instructions, small_style),
                ]],
                colWidths=[1.1 * inch, 5.2 * inch],
                style=TableStyle([
                    ("BACKGROUND", (0, 0), (-1, -1), colors.HexColor("#EFECE3")),
                    ("BOX", (0, 0), (-1, -1), 0.5, line),
                    ("VALIGN", (0, 0), (-1, -1), "TOP"),
                    ("LEFTPADDING", (0, 0), (-1, -1), 8),
                    ("RIGHTPADDING", (0, 0), (-1, -1), 8),
                    ("TOPPADDING", (0, 0), (-1, -1), 7),
                    ("BOTTOMPADDING", (0, 0), (-1, -1), 7),
                ]),
            ),
        ])

    story.extend([
        PageBreak(),
        Paragraph("Model reference", section_style),
    ])

    for model_index, model in enumerate(document["models"]):
        if model_index and model_index % 3 == 0:
            story.append(PageBreak())
        locations = "  /  ".join(f"{item['bankName']} {item['number']}" for item in model["locations"])
        model_name_and_badges = Table(
            [[
                Paragraph(_escape(display_name(
                    model["name"], model.get("customBank"), model.get("customModel"),
                )), model_style),
                fm_badges(model.get("fmCapabilities")),
            ]],
            colWidths=[2.68 * inch, 0.66 * inch],
            style=TableStyle([
                ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
                ("LEFTPADDING", (0, 0), (-1, -1), 0),
                ("RIGHTPADDING", (0, 0), (-1, -1), 0),
                ("TOPPADDING", (0, 0), (-1, -1), 0),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 0),
            ]),
        )
        title = Table(
            [[
                # Titled the same way the bank map lists it, so a reader can
                # look a customized slot up by the name the map gave it; the
                # bank's own name/author/patch count follow in the credit row.
                model_name_and_badges,
                Paragraph(_escape(locations), location_style),
            ]],
            colWidths=[3.4 * inch, 2.9 * inch],
            style=TableStyle([
                ("VALIGN", (0, 0), (-1, -1), "BOTTOM"),
                ("ALIGN", (1, 0), (1, 0), "RIGHT"),
                ("LEFTPADDING", (0, 0), (-1, -1), 0),
                ("RIGHTPADDING", (0, 0), (-1, -1), 0),
                ("TOPPADDING", (0, 0), (-1, -1), 0),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 2),
            ]),
        )
        # A customized FM slot no longer plays the factory patches the catalog
        # prose describes, so the bank's own description stands in for it and
        # HARMONICS is re-stated against the real patch count. Everything else
        # about the engine (TIMBRE, MORPH, the fourth macro, TRIG) is unchanged
        # by a bank swap and keeps its catalog prose.
        credit = model.get("customBank")
        custom_model = model.get("customModel")
        descriptions = model["manual"]["controls"]
        if credit:
            details_text = credit["description"] or (
                "This slot plays a custom six-operator FM bank in place of the built-in one."
            )
            descriptions = {
                **descriptions,
                "harmonics": "Selects one of the {} in this slot's custom bank.".format(
                    "single patch" if credit["patches"] == 1 else f"{credit['patches']} patches"
                ),
            }
        else:
            details_text = (
                f'This slot uses the recipe-baked “{custom_model["name"]}” '
                f'{"wave terrain" if custom_model["kind"] == "wave-terrain" else "wavetable"} data. '
                "Its panel controls behave like the original model. A compatible "
                "audio transfer through TIMBRE replaces this slot independently."
                if custom_model else model["description"]
            )
        details = Paragraph(_escape(details_text), body_style)
        parameter_rows: list[list[Any]] = [[
            Paragraph("PANEL", table_header_style),
            Paragraph("PARAMETER", table_header_style),
            Paragraph("WHAT IT DOES", table_header_style),
        ]]
        for index, control_id in enumerate(CONTROL_IDS):
            parameter_rows.append([
                Paragraph(PANEL_LABELS[index], table_header_style),
                Paragraph(_escape(model["controls"][index]), small_style),
                Paragraph(_escape(descriptions[control_id]), small_style),
            ])
        parameters = Table(parameter_rows, colWidths=[0.78 * inch, 1.38 * inch, 4.14 * inch], repeatRows=1)
        parameters.setStyle(TableStyle([
            ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#EFECE3")),
            ("GRID", (0, 0), (-1, -1), 0.35, line),
            ("VALIGN", (0, 0), (-1, -1), "TOP"),
            ("LEFTPADDING", (0, 0), (-1, -1), 5),
            ("RIGHTPADDING", (0, 0), (-1, -1), 5),
            ("TOPPADDING", (0, 0), (-1, -1), 4),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
        ]))
        behavior = Table(
            [
                [Paragraph("MAIN", table_header_style), Paragraph(_escape(model["outputs"][0]), small_style),
                 Paragraph("AUX", table_header_style), Paragraph(_escape(model["outputs"][1]), small_style)],
                [Paragraph("TRIG", table_header_style), Paragraph(_escape(model["manual"]["trigger"]), small_style), "", ""],
            ],
            colWidths=[0.42 * inch, 2.55 * inch, 0.42 * inch, 2.91 * inch],
            style=TableStyle([
                ("SPAN", (1, 1), (3, 1)),
                ("BACKGROUND", (0, 0), (-1, -1), paper),
                ("BOX", (0, 0), (-1, -1), 0.35, line),
                ("LINEBELOW", (0, 0), (-1, 0), 0.35, line),
                ("VALIGN", (0, 0), (-1, -1), "TOP"),
                ("LEFTPADDING", (0, 0), (-1, -1), 5),
                ("RIGHTPADDING", (0, 0), (-1, -1), 5),
                ("TOPPADDING", (0, 0), (-1, -1), 4),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
            ]),
        )
        story.extend([title, details, Spacer(1, 0.07 * inch)])
        if credit:
            byline = credit["name"] or "Untitled bank"
            if credit["author"]:
                byline += f" — {credit['author']}"
            if credit["origin"]:
                byline += f" ({credit['origin']})"
            patches = "1 patch" if credit["patches"] == 1 else f"{credit['patches']} patches"
            story.extend([
                Table(
                    [[
                        Paragraph("CUSTOM BANK", table_header_style),
                        Paragraph(
                            f"<b>{_escape(byline)}</b>"
                            f'<font color="#687069"> · {patches}</font>',
                            small_style,
                        ),
                    ]],
                    colWidths=[1.1 * inch, 5.2 * inch],
                    style=TableStyle([
                        ("BACKGROUND", (0, 0), (-1, -1), colors.HexColor("#EFECE3")),
                        ("BOX", (0, 0), (-1, -1), 0.35, line),
                        ("VALIGN", (0, 0), (-1, -1), "TOP"),
                        ("LEFTPADDING", (0, 0), (-1, -1), 5),
                        ("RIGHTPADDING", (0, 0), (-1, -1), 5),
                        ("TOPPADDING", (0, 0), (-1, -1), 4),
                        ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
                    ]),
                ),
                Spacer(1, 0.07 * inch),
            ])
        story.extend([
            parameters,
            behavior,
            Spacer(1, 0.22 * inch),
        ])

    class InvariantCanvas(Canvas):
        def __init__(self, *args: Any, **kwargs: Any) -> None:
            kwargs["invariant"] = 1
            super().__init__(*args, **kwargs)

    pdf = SimpleDocTemplate(
        str(output),
        pagesize=letter,
        leftMargin=margin,
        rightMargin=margin,
        topMargin=0.48 * inch,
        bottomMargin=0.52 * inch,
        title="Your Ro'Ved Field Guide" if roved else "Your Plaits Field Guide",
        author="Rubato Audio",
        subject="Recipe-specific Plaits Palette synthesis-model reference",
    )
    pdf.build(story, onFirstPage=footer, onLaterPages=footer, canvasmaker=InvariantCanvas)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recipe", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--build-key")
    args = parser.parse_args()
    recipe = json.loads(args.recipe.read_text(encoding="utf-8"))
    document = manual_document(recipe, args.build_key)
    render_pdf(document, args.output)
    print(f"wrote {len(document['models'])} model references to {args.output}")


if __name__ == "__main__":
    main()
