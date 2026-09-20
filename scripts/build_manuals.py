#!/usr/bin/env python3
from pathlib import Path
from reportlab.lib.pagesizes import A4
from reportlab.lib import colors
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.units import mm
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, PageBreak

ROOT=Path(__file__).resolve().parents[1]
DOCS=ROOT/"docs"

def lines_to_story(text, lang):
    styles=getSampleStyleSheet()
    title=ParagraphStyle("title",parent=styles["Title"],fontName="Helvetica-Bold",fontSize=22,leading=26,alignment=TA_CENTER,spaceAfter=14)
    body=ParagraphStyle("body",parent=styles["BodyText"],fontName="Helvetica",fontSize=9.5,leading=13,spaceAfter=6)
    h1=ParagraphStyle("h1",parent=styles["Heading1"],fontName="Helvetica-Bold",fontSize=16,leading=20,spaceBefore=10,spaceAfter=8)
    subtitle=ParagraphStyle("sub",parent=body,fontSize=11,leading=15,alignment=TA_CENTER,textColor=colors.HexColor("#444444"),spaceAfter=16)
    story=[]
    first=True
    for raw in text.splitlines():
        line=raw.strip()
        if not line:
            story.append(Spacer(1,4))
            continue
        if line.startswith("# "):
            story.append(Paragraph(line[2:],title)); first=False
        elif line.startswith("## "):
            story.append(Paragraph(line[3:],h1))
        elif line.startswith("**") and line.endswith("**"):
            story.append(Paragraph(line[2:-2],subtitle))
        elif line.startswith("- "):
            story.append(Paragraph("- "+line[2:].replace("**","<b>",1).replace("**","</b>",1),body))
        elif len(line)>2 and line[0].isdigit() and line[1]==".":
            story.append(Paragraph(line,body))
        else:
            safe=line.replace("**","<b>",1).replace("**","</b>",1)
            safe=safe.replace("`","")
            story.append(Paragraph(safe,body))
    return story

def make(src_name,out_name,lang):
    text=(DOCS/src_name).read_text(encoding="utf-8")
    doc=SimpleDocTemplate(str(DOCS/out_name),pagesize=A4,
        rightMargin=18*mm,leftMargin=18*mm,topMargin=18*mm,bottomMargin=18*mm)
    doc.build(lines_to_story(text,lang))

make("Ugly_Reverb_Handbuch_DE.md","Ugly_Reverb_Handbuch_DE.pdf","DE")
make("Ugly_Reverb_Manual_EN.md","Ugly_Reverb_Manual_EN.pdf","EN")
print("Generated German and English PDF manuals")
