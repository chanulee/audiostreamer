"""Build the AS-008 stand in millimeters. Run with the rendering Python environment."""

from math import cos, radians, tan
from pathlib import Path
from struct import iter_unpack, pack
import sys

from OCP.BRepAlgoAPI import BRepAlgoAPI_Cut
from OCP.BRepBuilderAPI import BRepBuilderAPI_MakeFace, BRepBuilderAPI_MakePolygon
from OCP.BRepCheck import BRepCheck_Analyzer
from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.BRepPrimAPI import BRepPrimAPI_MakeBox, BRepPrimAPI_MakeCylinder, BRepPrimAPI_MakePrism
from OCP.IFSelect import IFSelect_RetDone
from OCP.STEPControl import STEPControl_AsIs, STEPControl_Writer
from OCP.StlAPI import StlAPI_Writer
from OCP.TopAbs import TopAbs_SOLID
from OCP.TopExp import TopExp_Explorer
from OCP.gp import gp_Ax2, gp_Dir, gp_Pnt, gp_Vec

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "rendering"))
import render_svg as kicad

TILT = 10
SIDE_GAP = 0.25
RIM_WIDTH = 1.4
SEAT_DEPTH = 0.5
LOW_END_HEIGHT = 5.5
PILOT_DIAMETER = 1.75
PILOT_DEPTH = 3.8


def box(x, y, z, width, length, height):
    return BRepPrimAPI_MakeBox(gp_Pnt(x, y, z), width, length, height).Shape()


def side_prism(profile, half_width):
    outline = BRepBuilderAPI_MakePolygon()
    for y, z in profile:
        outline.Add(gp_Pnt(-half_width, y, z))
    outline.Close()
    side = BRepBuilderAPI_MakeFace(outline.Wire()).Face()
    return BRepPrimAPI_MakePrism(side, gp_Vec(2 * half_width, 0, 0)).Shape()


def build_stand(board_path, tilt=TILT, low_end=LOW_END_HEIGHT, make_blank=None, *, mount_y=0):
    kicad.load_occ()
    board = kicad.parse_board(board_path.read_text())
    # Use Edge.Cuts and the H1 footprint origin in Y-up board space.
    edges = [edge for edge in kicad.children(board, "gr_line")
             if kicad.field(edge, "layer") == ["Edge.Cuts"]]
    points = [tuple(kicad.numbers(edge, key)) for edge in edges for key in ("start", "end")]
    xs, ys = sorted(set(x for x, _ in points)), sorted(set(y for _, y in points))
    if len(edges) != 4 or len(xs) != 2 or len(ys) != 2 or any(points.count(p) != 2 for p in set(points)):
        raise ValueError("The stand requires a rectangular PCB")
    half_w, half_l = (xs[1] - xs[0]) / 2, (ys[1] - ys[0]) / 2
    cx, cy = xs[0] + half_w, ys[0] + half_l
    mount = next(fp for fp in kicad.children(board, "footprint") if kicad.reference(fp) == "H1")
    x, y = kicad.numbers(mount, "at")[:2]
    hx, hy = x - cx, cy - y

    outer_x = half_w + SIDE_GAP + RIM_WIDTH
    outer_y = (half_l + SIDE_GAP + RIM_WIDTH) * cos(radians(tilt))
    rise_per_mm = tan(radians(tilt))
    high_end = low_end + 2 * outer_y * rise_per_mm
    mid_height = (low_end + high_end) / 2
    if make_blank is None:
        profile = ((-outer_y, 0), (outer_y, 0), (outer_y, low_end), (-outer_y, high_end))
        blank = side_prism(profile, outer_x)
    else:
        blank = make_blank(outer_x, outer_y, low_end, rise_per_mm)

    def on_slope(shape, orientation=0):
        shape = kicad.moved(shape, angles=(0, 0, orientation))
        return kicad.moved(shape, (0, mount_y, mid_height), (-tilt, 0, 0))

    stand = cut_pcb_mount(blank, half_w, half_l, hx, hy, on_slope)
    return stand, on_slope


def cut_pcb_mount(blank, half_w, half_l, hx, hy, on_slope):
    seat_x, seat_y = half_w + SIDE_GAP, half_l + SIDE_GAP
    seat = box(-seat_x, -seat_y, -SEAT_DEPTH, 2 * seat_x, 2 * seat_y, 10)
    stand = BRepAlgoAPI_Cut(blank, on_slope(seat)).Shape()
    for orientation in (0, 180):
        axis = gp_Ax2(gp_Pnt(hx, hy, -SEAT_DEPTH - PILOT_DEPTH), gp_Dir(0, 0, 1))
        pilot = BRepPrimAPI_MakeCylinder(axis, PILOT_DIAMETER / 2, PILOT_DEPTH + 1).Shape()
        stand = BRepAlgoAPI_Cut(stand, on_slope(pilot, orientation)).Shape()

    return stand


def export_stand(stand, target):
    solids = TopExp_Explorer(stand, TopAbs_SOLID)
    if not solids.More() or not BRepCheck_Analyzer(stand).IsValid():
        raise ValueError("Stand is not a valid solid")
    solids.Next()
    if solids.More():
        raise ValueError("Stand must be one connected solid")
    step = STEPControl_Writer()
    if step.Transfer(stand, STEPControl_AsIs) != IFSelect_RetDone:
        raise RuntimeError("STEP transfer failed")
    if step.Write(str(target.with_suffix(".step"))) != IFSelect_RetDone:
        raise RuntimeError("STEP export failed")

    BRepMesh_IncrementalMesh(stand, 0.02, False, 0.12)
    stl = StlAPI_Writer()
    stl.ASCIIMode = False
    stl_path = target.with_suffix(".stl")
    if not stl.Write(stand, str(stl_path)):
        raise RuntimeError("STL export failed")
    # OCCT emits collapsed facets where some rounded surfaces meet.
    mesh = stl_path.read_bytes()
    facets = [facet for facet in iter_unpack("<12fH", mesh[84:])
              if len({facet[3:6], facet[6:9], facet[9:12]}) == 3]
    if 50 * len(facets) != len(mesh) - 84:
        stl_path.write_bytes(mesh[:80] + pack("<I", len(facets))
                             + b"".join(pack("<12fH", *facet) for facet in facets))


def main():
    stand, _ = build_stand(HERE.parent / "lovelace-as-008.kicad_pcb")
    export_stand(stand, HERE / "lovelace-as-008-stand")


if __name__ == "__main__":
    main()
