"""Build the AS-008 sketch models in millimeters."""

from math import cos, radians, sin, tan, tau
from pathlib import Path
import sys

from OCP.BRepAlgoAPI import BRepAlgoAPI_Cut, BRepAlgoAPI_Fuse
from OCP.BRepBuilderAPI import (
    BRepBuilderAPI_MakeEdge, BRepBuilderAPI_MakeFace, BRepBuilderAPI_MakePolygon, BRepBuilderAPI_MakeWire,
)
from OCP.BRepOffsetAPI import BRepOffsetAPI_ThruSections
from OCP.BRepFilletAPI import BRepFilletAPI_MakeFillet
from OCP.BRepPrimAPI import BRepPrimAPI_MakeCone, BRepPrimAPI_MakeCylinder, BRepPrimAPI_MakeRevol
from OCP.Geom import Geom_BezierCurve
from OCP.TColgp import TColgp_Array1OfPnt
from OCP.TopAbs import TopAbs_EDGE
from OCP.TopExp import TopExp
from OCP.TopTools import TopTools_IndexedMapOfShape
from OCP.TopoDS import TopoDS
from OCP.gp import gp_Ax1, gp_Ax2, gp_Dir, gp_Pnt

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from build_stand import box, build_stand, cut_pcb_mount, export_stand, kicad, side_prism

TOP_TILT = 17
LOW_END_HEIGHT = 11
BODY_VERTICAL_THICKNESS = 13.5
VERTICAL_BRACKET_TOP_DROP = 4.4
DIAGONAL_TILT = 24
DIAGONAL_LOW_END = 5.6
DIAGONAL_END_ANGLE = radians(30)
DIAGONAL_SLIDE = 2.2
BASE_HEIGHT = 60
NECK_HEIGHT = 50
BASE_DIAMETER = 72
NECK_DIAMETER = 48
SLOT_WIDTH = 8
SLOT_FLOOR = 3
SLOT_BOARD_BOTTOM = 65.5
SLOT_BOARD_BACK = 3.25
UPRIGHT_CENTER = 34


def rounded(shape, radius):
    edges = TopTools_IndexedMapOfShape()
    TopExp.MapShapes_s(shape, TopAbs_EDGE, edges)
    fillet = BRepFilletAPI_MakeFillet(shape)
    for index in range(1, edges.Extent() + 1):
        fillet.Add(radius, TopoDS.Edge_s(edges.FindKey(index)))
    fillet.Build()
    if not fillet.IsDone():
        raise ValueError(f"Could not round exterior edges to R{radius}")
    return fillet.Shape()


def bracket_blank(half_width, half_run, low_end, slope, lean, top_drop=0):
    run = 2 * half_run
    lean_per_mm = tan(radians(lean))
    underside = low_end - BODY_VERTICAL_THICKNESS
    high_end = low_end + run * slope
    # Intersect the end walls with the underside, which is parallel to the top.
    drop = BODY_VERTICAL_THICKNESS / (1 - slope * lean_per_mm)
    front_under = -drop * lean_per_mm
    rear_under = run - drop * lean_per_mm
    body_profile = [
        (half_run, low_end - top_drop),
        (-half_run, high_end - top_drop),
        (half_run - rear_under, high_end - drop),
        (half_run - front_under, low_end - drop),
    ]
    body = rounded(side_prism(body_profile, half_width), 0.45 if lean else 0.55)

    shoulder = run * (0.66 if lean else 0.74)
    toe = run * 0.74
    overlap = 4.5
    front_join = -(BODY_VERTICAL_THICKNESS - overlap) * lean_per_mm / (1 - slope * lean_per_mm)
    foot_profile = [
        (half_run + (low_end + 1) * lean_per_mm, -1),
        (half_run - front_join, underside + front_join * slope + overlap),
        (half_run - shoulder, underside + shoulder * slope + overlap),
        (half_run - toe, -1),
    ]
    foot = rounded(side_prism(foot_profile, half_width / 3), 0.5)
    joined = BRepAlgoAPI_Fuse(body, foot)
    joined.SimplifyResult()
    return BRepAlgoAPI_Cut(joined.Shape(), box(-60, -60, -20, 120, 120, 20)).Shape()


def build_bracket(board, lean):
    top_drop = VERTICAL_BRACKET_TOP_DROP if lean == 0 else 0
    return build_stand(
        board, tilt=TOP_TILT, low_end=LOW_END_HEIGHT - top_drop,
        make_blank=lambda width, run, low, slope: bracket_blank(
            width, run, low + top_drop, slope, lean, top_drop,
        ),
    )


def round_shoulder():
    start = BASE_HEIGHT - 6
    slope = (BASE_DIAMETER - NECK_DIAMETER) / (2 * (start + 2))
    return start, slope


def turned_body():
    base, neck = BASE_DIAMETER / 2, NECK_DIAMETER / 2
    top = BASE_HEIGHT + NECK_HEIGHT
    profile = BRepBuilderAPI_MakeWire()

    def edge(*points):
        coordinates = [gp_Pnt(r, 0, z) for r, z in points]
        if len(coordinates) == 2:
            profile.Add(BRepBuilderAPI_MakeEdge(*coordinates).Edge())
        else:
            poles = TColgp_Array1OfPnt(1, len(coordinates))
            for index, point in enumerate(coordinates, 1):
                poles.SetValue(index, point)
            profile.Add(BRepBuilderAPI_MakeEdge(Geom_BezierCurve(poles)).Edge())

    edge((0, 0), (base - 0.8, 0))
    edge((base - 0.8, 0), (base, 1))
    shoulder_start, slope = round_shoulder()
    edge((base, 1), (neck + 3 * slope, shoulder_start))
    edge((neck + 3 * slope, shoulder_start), (neck, shoulder_start + 3), (neck, BASE_HEIGHT))
    edge((neck, BASE_HEIGHT), (neck, top - 0.7))
    edge((neck, top - 0.7), (neck - 0.7, top))
    edge((neck - 0.7, top), (0, top))
    edge((0, top), (0, 0))
    face = BRepBuilderAPI_MakeFace(profile.Wire()).Face()
    return BRepPrimAPI_MakeRevol(face, gp_Ax1(gp_Pnt(), gp_Dir(0, 0, 1)), tau).Shape()


def on_vertical_pcb(shape, x, z, orientation=0):
    shape = kicad.moved(shape, angles=(0, 0, orientation))
    return kicad.moved(shape, (x, 0, z), (-90, 0, 90))


def on_slot_pcb(shape, lift=0):
    return on_vertical_pcb(shape, SLOT_BOARD_BACK, SLOT_BOARD_BOTTOM + 20.5 + lift, 180)


def round_model():
    kicad.load_occ()
    top = BASE_HEIGHT + NECK_HEIGHT
    radius = SLOT_WIDTH / 2
    slot_axis = gp_Ax2(gp_Pnt(0, -BASE_DIAMETER, SLOT_FLOOR + radius), gp_Dir(0, 1, 0))
    slot_bottom = BRepPrimAPI_MakeCylinder(slot_axis, radius, 2 * BASE_DIAMETER).Shape()
    slot_top = box(-radius, -BASE_DIAMETER, SLOT_FLOOR + radius, SLOT_WIDTH, 2 * BASE_DIAMETER, top)
    body = BRepAlgoAPI_Cut(turned_body(), BRepAlgoAPI_Fuse(slot_top, slot_bottom).Shape()).Shape()

    # The lower corners sit in guides outside the two downward-facing sockets.
    floor = SLOT_BOARD_BOTTOM
    guide_profile = [(4.8, floor - 5.5), (0.2, floor - 0.9),
                     (0.2, floor + 5.5), (4.8, floor + 5.5)]
    guide = rounded(side_prism(guide_profile, 2.2), 0.35)
    for y in (-20.2, 20.2):
        body = BRepAlgoAPI_Fuse(body, kicad.moved(guide, (0, y, 0), (0, 0, -90))).Shape()

    left, right = SLOT_BOARD_BACK - 1.85, SLOT_BOARD_BACK + 0.25
    guide_channel = [(left, floor), (right, floor), (right, floor + 4.3),
                     (right + 0.65, floor + 5.5), (right + 0.65, floor + 8),
                     (left - 0.65, floor + 8), (left - 0.65, floor + 5.5),
                     (left, floor + 4.3)]
    channel = kicad.moved(side_prism(guide_channel, 18.75), angles=(0, 0, -90))
    body = BRepAlgoAPI_Cut(body, channel).Shape()

    # A diagonal lead-in across the slot lips.
    entry = [(4, top - 2.8), (6.8, top), (6.8, top + 2), (4, top + 2)]
    lead_in = kicad.moved(side_prism(entry, 27), angles=(0, 0, -90))
    for orientation in (0, 180):
        body = BRepAlgoAPI_Cut(body, kicad.moved(lead_in, angles=(0, 0, orientation))).Shape()
    # Follow the taper and the shoulder with the same cross-section as the neck bevel.
    shoulder_start, taper = round_shoulder()
    sections = [(SLOT_FLOOR, (BASE_HEIGHT - SLOT_FLOOR - 3) * taper + 4),
                (SLOT_FLOOR + radius, (BASE_HEIGHT - SLOT_FLOOR - radius - 3) * taper)]
    for z in (shoulder_start, shoulder_start + 1.5, shoulder_start + 3,
              shoulder_start + 4.5, BASE_HEIGHT):
        t = (BASE_HEIGHT - z) / (BASE_HEIGHT - shoulder_start)
        sections.append((z, 3 * taper * t * t))
    sections.append((top + 2, 0))
    lip = ((3.6, 20.8), (4.0, 20.8), (9.8, 26.6), (3.6, 26.6))
    for sx, sy in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
        cutter = BRepOffsetAPI_ThruSections(True, True)
        for z, offset in sections:
            wire = BRepBuilderAPI_MakePolygon()
            for x, y in lip:
                wire.Add(gp_Pnt(sx * x, sy * (y + offset), z))
            wire.Close()
            cutter.AddWire(wire.Wire())
        cutter.Build()
        body = BRepAlgoAPI_Cut(body, cutter.Shape()).Shape()
    # The taper closes the cable chamber above the outlet while leaving a
    # printable slope. Each socket has its own clearance pocket above it.
    plenum_axis = gp_Ax2(gp_Pnt(0, 0, 7), gp_Dir(0, 0, 1))
    roof_axis = gp_Ax2(gp_Pnt(0, 0, 40), gp_Dir(0, 0, 1))
    plenum = BRepPrimAPI_MakeCylinder(plenum_axis, 23, 33).Shape()
    roof = BRepPrimAPI_MakeCone(roof_axis, 23, 9, 20).Shape()
    body = BRepAlgoAPI_Cut(body, BRepAlgoAPI_Fuse(plenum, roof).Shape()).Shape()
    for y in (-13.2, 13.2):
        plug_space = rounded(box(-7.5, y - 6.4, 37, 15, 12.8, floor - 38), 0.7)
        body = BRepAlgoAPI_Cut(body, plug_space).Shape()
    # Connect two semicircular ends with a straight band for the rear capsule.
    passage = box(14, -7, 15.5, 30, 14, 9)
    for z in (15.5, 24.5):
        axis = gp_Ax2(gp_Pnt(14, 0, z), gp_Dir(1, 0, 0))
        passage = BRepAlgoAPI_Fuse(passage, BRepPrimAPI_MakeCylinder(axis, 7, 30).Shape()).Shape()
    return BRepAlgoAPI_Cut(body, passage).Shape()


def skirt_blank(half_width, half_run, low_end, slope, *, base_half_width=27.5,
                base_x=0, base_y=0):
    loft = BRepOffsetAPI_ThruSections(True, True)
    for w, l, height, pitch, dx, dy in (
        (base_half_width, 29.5, 0, 0, base_x, base_y),
        (half_width, half_run, low_end + half_run * slope, slope, 0, 0),
    ):
        wire = BRepBuilderAPI_MakePolygon()
        for x, y in ((-w, -l), (w, -l), (w, l), (-w, l)):
            wire.Add(gp_Pnt(x + dx, y + dy, height - pitch * y))
        wire.Close()
        loft.AddWire(wire.Wire())
    loft.Build()
    return rounded(loft.Shape(), 0.45)


def build_offset_skirt(board):
    return build_stand(
        board, tilt=0, low_end=6,
        make_blank=lambda w, l, h, s: skirt_blank(w, l, h, s, base_half_width=29.5,
                                                  base_x=2.5, base_y=5),
    )


def on_upright_pcb(shape, orientation=0):
    return kicad.moved(on_vertical_pcb(shape, 0, UPRIGHT_CENTER, orientation), angles=(0, 0, 180))


def upright_model():
    panel = rounded(box(-16, -21, 10, 16, 42, 48), 0.55)
    counterbody = rounded(BRepPrimAPI_MakeCylinder(20, 26).Shape(), 0.9)
    counterbody = BRepAlgoAPI_Cut(counterbody, box(-22, -22, -1, 22, 44, 28)).Shape()
    counterbody = kicad.moved(counterbody, (10, 0, 8))
    # The two bodies are offset and joined below by the U in the side sketch.
    foot = [(13, 0), (-23, 0), (-23, 11), (-16, 11),
            (-16, 5), (5, 5), (5, 13), (13, 13)]
    base = kicad.moved(rounded(side_prism(foot, 15), 0.5), angles=(0, 0, 90))
    body = BRepAlgoAPI_Fuse(BRepAlgoAPI_Fuse(panel, base).Shape(), counterbody).Shape()
    body = cut_pcb_mount(body, 18.5, 20.5, 15.75, 17.75, on_upright_pcb)
    for y in (-13.2, 13.2):
        body = BRepAlgoAPI_Cut(body, box(-1.1, y - 6.5, 54.5, 2.5, 13, 5)).Shape()
    return body


def diagonal_blank(half_width, half_run, low_end, slope):
    run = 2 * half_run
    slide_y = DIAGONAL_SLIDE * cos(DIAGONAL_END_ANGLE)
    slide_z = DIAGONAL_SLIDE * sin(DIAGONAL_END_ANGLE)
    high_end = low_end + run * slope - slide_z
    # Extend the nose below the cut plane so the low end lands on a flat strip.
    stock_floor = -0.8
    end_slope = tan(DIAGONAL_END_ANGLE)
    advance = (low_end - stock_floor) / end_slope
    high_y = -half_run - slide_y
    trim = 0.7
    upper_profile = [(half_run - slide_y, low_end - slide_z),
                     (high_y + trim / slope, high_end - trim),
                     (high_y - trim / end_slope, high_end - trim),
                     (-half_run - advance, stock_floor + run * slope),
                     (half_run - advance, stock_floor)]
    body = rounded(side_prism(upper_profile, half_width), 0.25)

    # Continue the same 30° slope through the floor and 1.5 mm into the body.
    toe_at_floor = run * 0.88
    shoulder = (toe_at_floor * end_slope + advance * slope - stock_floor - 1.5) / (slope + end_slope)
    shoulder_height = (toe_at_floor - shoulder) * end_slope
    toe_trim = 1.5
    toe_cut_y = half_run - toe_at_floor + toe_trim
    foot_profile = [(half_run - advance - 0.8, 0),
                    (half_run - shoulder, shoulder_height),
                    (toe_cut_y, toe_trim * end_slope),
                    (toe_cut_y, 0)]
    foot = rounded(side_prism(foot_profile, half_width / 3), 0.2)
    joined = BRepAlgoAPI_Fuse(body, foot)
    joined.SimplifyResult()
    return BRepAlgoAPI_Cut(joined.Shape(), box(-60, -60, -20, 120, 120, 20)).Shape()


def build_diagonal(board):
    return build_stand(
        board, tilt=DIAGONAL_TILT,
        low_end=DIAGONAL_LOW_END - DIAGONAL_SLIDE * sin(DIAGONAL_END_ANGLE),
        make_blank=lambda w, r, h, s: diagonal_blank(w, r, DIAGONAL_LOW_END, s),
        mount_y=-DIAGONAL_SLIDE * cos(DIAGONAL_END_ANGLE),
    )


def main():
    board = HERE.parent.parent / "lovelace-as-008.kicad_pcb"
    for number, lean in ((1, 0), (2, 15)):
        stand, _ = build_bracket(board, lean)
        export_stand(stand, HERE / f"sketch-{number:02}-pcb-stand")
    export_stand(round_model(), HERE / "sketch-03-round")
    skirt_2, _ = build_offset_skirt(board)
    export_stand(skirt_2, HERE / "sketch-04-2-offset")
    export_stand(upright_model(), HERE / "sketch-05-upright")
    diagonal, _ = build_diagonal(board)
    export_stand(diagonal, HERE / "sketch-06-diagonal")


if __name__ == "__main__":
    main()
