#!/usr/bin/env python3
"""Render a recipe-specific Plaits Lab field guide as a deterministic PDF."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from generate_engine_config import validate_recipe


BUILDER_DIR = Path(__file__).resolve().parent
PUBLIC_CATALOG_PATH = BUILDER_DIR.parent / "plaits_lab_catalog/public_catalog.json"
BANKS = (
    {"id": "green", "name": "GREEN", "start": 0, "color": "#4F9868"},
    {"id": "red", "name": "RED", "start": 8, "color": "#C6534B"},
    {"id": "amber", "name": "AMBER", "start": 16, "color": "#D59635"},
    # The opt-in fourth bank; matches the editor's orange (plaits-palette.css).
    {"id": "orange", "name": "ORANGE", "start": 24, "color": "#D96F35"},
)
CONTROL_IDS = ("harmonics", "timbre", "morph", "macro")
PANEL_LABELS = ("HARMONICS", "TIMBRE", "MORPH", "FOURTH")


def load_catalog() -> dict[str, Any]:
    return json.loads(PUBLIC_CATALOG_PATH.read_text(encoding="utf-8"))


def position(slot: int) -> dict[str, Any]:
    bank = BANKS[slot // 8]
    return {"bank": bank["id"], "bankName": bank["name"], "color": bank["color"], "number": slot % 8 + 1}


def manual_document(recipe: Any, build_key: str | None = None) -> dict[str, Any]:
    slots = validate_recipe(recipe).public_slots
    catalog = load_catalog()
    by_id = {engine["id"]: engine for engine in catalog["engines"]}
    models: list[dict[str, Any]] = []
    seen: set[str] = set()
    for slot, engine_id in enumerate(slots):
        if engine_id in seen:
            continue
        seen.add(engine_id)
        engine = by_id[engine_id]
        locations = [position(index) for index, value in enumerate(slots) if value == engine_id]
        models.append({**engine, "locations": locations})
    return {
        "buildKey": build_key,
        "slots": [
            {"engine": by_id[engine_id], "position": position(slot)}
            for slot, engine_id in enumerate(slots)
        ],
        "models": models,
    }


def _escape(value: str) -> str:
    return value.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


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

    def footer(canvas: Canvas, doc: SimpleDocTemplate) -> None:
        canvas.saveState()
        canvas.setStrokeColor(line)
        canvas.setLineWidth(0.4)
        canvas.line(margin, 0.41 * inch, page_width - margin, 0.41 * inch)
        canvas.setFillColor(muted)
        canvas.setFont("Helvetica", 6.8)
        canvas.drawString(margin, 0.25 * inch, "PLAITS LAB")
        canvas.drawRightString(page_width - margin, 0.25 * inch, str(doc.page))
        canvas.restoreState()

    bank_count = len(document["slots"]) // 8
    bank_phrase = (
        "green, red, and amber"
        if bank_count == 3
        else "green, red, amber, and orange"
    )
    story: list[Any] = [
        Paragraph("RUBATO AUDIO  /  PLAITS LAB", kicker_style),
        Paragraph("Your Plaits Field Guide", title_style),
        Paragraph(
            f"A rack-side reference generated from the exact {bank_phrase} layout in this firmware recipe. "
            f"This guide contains {len(document['models'])} unique synthesis models.",
            intro_style,
        ),
        Paragraph("Bank map", section_style),
    ]

    bank_tables = []
    for bank_index, bank in enumerate(BANKS[:bank_count]):
        rows: list[list[Any]] = [[Paragraph(bank["name"], bank_name_style)]]
        for bank_slot in range(8):
            entry = document["slots"][bank_index * 8 + bank_slot]
            engine = entry["engine"]
            rows.append([
                Table(
                    [[Paragraph(f"{bank_slot + 1:02d}", small_muted_style), Paragraph(_escape(engine["name"]), bank_model_style)]],
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
        bank_table = Table(rows, colWidths=[2.05 * inch], rowHeights=[0.26 * inch] + [0.31 * inch] * 8)
        bank_table.setStyle(TableStyle([
            ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor(bank["color"])),
            ("BACKGROUND", (0, 1), (-1, -1), colors.white),
            ("BOX", (0, 0), (-1, -1), 0.6, colors.HexColor(bank["color"])),
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
    story.extend([
        bank_map,
        Spacer(1, 0.18 * inch),
        Table(
            [[
                Paragraph("FOURTH CONTROL", table_header_style),
                Paragraph(
                    "Hold the right model button and turn HARMONICS until the model LEDs blink yellow; this selects the octave-switching frequency range. "
                    "Short-press both model buttons to open the alternate-firmware options menu. Use the left button to select the first light, then press the right button until it blinks green. "
                    "Press both buttons again to exit. The FREQUENCY knob now controls the selected model's fourth parameter; for Mutable Instruments models, noon preserves the original sound.",
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
        PageBreak(),
        Paragraph("Model reference", section_style),
    ])

    for model_index, model in enumerate(document["models"]):
        if model_index and model_index % 3 == 0:
            story.append(PageBreak())
        locations = "  /  ".join(f"{item['bankName']} {item['number']}" for item in model["locations"])
        title = Table(
            [[
                Paragraph(_escape(model["name"]), model_style),
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
        details = Paragraph(_escape(model["description"]), body_style)
        parameter_rows: list[list[Any]] = [[
            Paragraph("PANEL", table_header_style),
            Paragraph("PARAMETER", table_header_style),
            Paragraph("WHAT IT DOES", table_header_style),
        ]]
        descriptions = model["manual"]["controls"]
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
        story.extend([
            title,
            details,
            Spacer(1, 0.07 * inch),
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
        title="Your Plaits Field Guide",
        author="Rubato Audio",
        subject="Recipe-specific Plaits Lab synthesis-model reference",
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
