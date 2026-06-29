#!/usr/bin/env python3
import argparse
import html
import math
import re
from pathlib import Path


VECTOR_RE = re.compile(r"(\w+)=\((-?\d+(?:\.\d+)?),(-?\d+(?:\.\d+)?),(-?\d+(?:\.\d+)?)\)")
SCALAR_RE = re.compile(r"(\w+)=([^\s]+)")
TARGET_CONTRACT = "aimPivotAimForwardGripRoll_holdHardpoint_freshPose_noFallback"


def parse_vector(text):
    return tuple(float(part) for part in text.split(","))


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def mul(a, scale):
    return (a[0] * scale, a[1] * scale, a[2] * scale)


def length(v):
    return math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])


def fmt(v):
    return f"({v[0]:.5f}, {v[1]:.5f}, {v[2]:.5f})"


def parse_proof_line(line):
    if "SWGVRHandsProof " not in line:
        return None

    record = {}
    for key, x, y, z in VECTOR_RE.findall(line):
        record[key] = (float(x), float(y), float(z))

    scrubbed = VECTOR_RE.sub("", line)
    for key, value in SCALAR_RE.findall(scrubbed):
        if key == "SWGVRHandsProof":
            continue
        record[key] = value

    if record.get("kind") not in ("controller", "mesh", "hardpointPivot"):
        return None
    if "hand" not in record:
        return None

    try:
        record["hand"] = int(record["hand"])
    except ValueError:
        return None

    if "deltaMag" in record:
        try:
            record["deltaMag"] = float(record["deltaMag"])
        except ValueError:
            record["deltaMag"] = None
    if "hardpointMinusTargetMag" in record:
        try:
            record["hardpointMinusTargetMag"] = float(record["hardpointMinusTargetMag"])
        except ValueError:
            record["hardpointMinusTargetMag"] = None
    if "targetMinusAimMag" in record:
        try:
            record["targetMinusAimMag"] = float(record["targetMinusAimMag"])
        except ValueError:
            record["targetMinusAimMag"] = None
    for scalar in (
        "handMinusRayOriginMag",
        "gripMinusRayOriginMag",
        "rayMinusSharedPivotMag",
        "softwareGripMinusSharedPivotMag",
        "handMinusSharedPivotMag",
        "handForwardMinusRayForwardMag",
        "openxrGripOriginMinusSharedPivotMag",
        "meshTrackedMinusHandPivotMag",
        "renderedHardpointMinusHandPivotMag",
        "renderedHardpointMinusSharedPivotMag",
        "predictedHardpointMinusTargetMag",
    ):
        if scalar in record:
            try:
                record[scalar] = float(record[scalar])
            except ValueError:
                record[scalar] = None

    alias_vectors = {
        "sharedPivot_p": "aim_p",
        "rayOrigin_p": "aim_p",
        "rayForward_p": "aim_f",
        "softwareGripPivot_p": "software_grip_p",
        "openxrGripOrigin_p": "grip_p",
        "gripOrigin_p": "grip_p",
        "handPivot_p": "target_p",
        "hand_i": "target_i",
        "hand_j": "target_j",
        "hand_k": "target_k",
        "meshTrackedPivot_p": "solved_p",
        "sharedPivot_o": "aim_p",
        "rayOrigin_o": "aim_p",
        "softwareGripPivot_o": "software_grip_p",
        "openxrGripOrigin_o": "grip_p",
        "renderedHardpoint_o": "hardpoint_o",
        "handPivot_o": "target_o",
    }
    for source, target in alias_vectors.items():
        if source in record and target not in record:
            record[target] = record[source]

    alias_scalars = {
        "handMinusRayOriginMag": "targetMinusAimMag",
        "handMinusSharedPivotMag": "targetMinusAimMag",
        "meshTrackedMinusHandPivotMag": "deltaMag",
        "renderedHardpointMinusHandPivotMag": "hardpointMinusTargetMag",
    }
    for source, target in alias_scalars.items():
        if source in record and target not in record:
            record[target] = record[source]

    return record


def latest_records(trace_path):
    controller = {}
    mesh = {}
    hardpoint = {}
    if not trace_path.exists():
        return controller, mesh, hardpoint

    with trace_path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            record = parse_proof_line(line)
            if not record:
                continue
            if record["kind"] == "controller":
                controller[record["hand"]] = record
            elif record["kind"] == "mesh":
                mesh[(record["hand"], record.get("role", "mesh"))] = record
            elif record["kind"] == "hardpointPivot":
                hardpoint[record["hand"]] = record
    return controller, mesh, hardpoint


class Projector:
    def __init__(self, points, width=1100, height=720):
        self.width = width
        self.height = height
        if points:
            self.center = (
                sum(p[0] for p in points) / len(points),
                sum(p[1] for p in points) / len(points),
                sum(p[2] for p in points) / len(points),
            )
        else:
            self.center = (0.0, 0.0, 0.0)

        projected = [self.raw_project(p) for p in points] or [(0.0, 0.0)]
        max_extent = max(max(abs(x), abs(y)) for x, y in projected)
        self.scale = 260.0 / max(max_extent, 0.25)

    def raw_project(self, p):
        x, y, z = sub(p, self.center)
        return (x - z * 0.55, -y + z * 0.35)

    def project(self, p):
        x, y = self.raw_project(p)
        return (self.width * 0.5 + x * self.scale, self.height * 0.54 + y * self.scale)


def svg_line(projector, a, b, color, width=3, dash="", label=None):
    ax, ay = projector.project(a)
    bx, by = projector.project(b)
    dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
    parts = [f'<line x1="{ax:.1f}" y1="{ay:.1f}" x2="{bx:.1f}" y2="{by:.1f}" stroke="{color}" stroke-width="{width}" stroke-linecap="round"{dash_attr}/>' ]
    if label:
        parts.append(f'<text x="{bx + 6:.1f}" y="{by - 6:.1f}" class="label">{html.escape(label)}</text>')
    return "\n".join(parts)


def svg_dot(projector, p, color, label, radius=6):
    x, y = projector.project(p)
    return (
        f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{radius}" fill="{color}" stroke="#111827" stroke-width="2"/>'
        f'<text x="{x + 9:.1f}" y="{y - 9:.1f}" class="label">{html.escape(label)}</text>'
    )


def draw_axes(projector, origin, axes, prefix, alpha=1.0):
    axis_len = 0.22
    opacity = f' opacity="{alpha:.2f}"'
    parts = [f"<g{opacity}>"]
    colors = {"i": "#ef4444", "j": "#22c55e", "k": "#3b82f6"}
    for axis in ("i", "j", "k"):
        key = f"{prefix}_{axis}"
        if key in axes:
            parts.append(svg_line(projector, origin, add(origin, mul(axes[key], axis_len)), colors[axis], 3, label=f"{prefix}.{axis.upper()}"))
    parts.append("</g>")
    return "\n".join(parts)


def collect_points(controller, mesh, hardpoint):
    points = []
    for record in list(controller.values()) + list(mesh.values()) + list(hardpoint.values()):
        for key, value in record.items():
            if key.startswith("delta"):
                continue
            if key.endswith("_p") or key.endswith("_w"):
                if isinstance(value, tuple):
                    points.append(value)
        if record.get("aim_p") and record.get("aim_f"):
            points.append(add(record["aim_p"], mul(record["aim_f"], 0.75)))
    return points


def render_svg(controller, mesh, hardpoint):
    points = collect_points(controller, mesh, hardpoint)
    projector = Projector(points)
    parts = [
        f'<svg viewBox="0 0 {projector.width} {projector.height}" role="img" aria-label="VR hand controller math proof">',
        "<style>",
        ".label{font:13px Segoe UI,Arial,sans-serif;fill:#111827;font-weight:650}",
        ".small{font:12px Segoe UI,Arial,sans-serif;fill:#374151}",
        "</style>",
        '<rect x="0" y="0" width="1100" height="720" fill="#f8fafc"/>',
        '<text x="28" y="38" class="label">VR hand proof: aim pivot drives hand position; aim ray drives finger direction; grip pose drives roll</text>',
        '<text x="28" y="62" class="small">Red/green/blue axes are local I/J/K. Yellow line is aim ray. Magenta line is mesh bind error delta.</text>',
    ]

    mesh_hands = {key[0] for key in mesh.keys()}
    for hand in sorted(set(controller.keys()) | mesh_hands):
        hand_name = "left" if hand == 0 else "right"
        c = controller.get(hand)
        terminal = mesh.get((hand, "terminal"))
        support = mesh.get((hand, "skinSupport"))
        h = hardpoint.get(hand)
        if c:
            if "grip_p" in c:
                parts.append(svg_dot(projector, c["grip_p"], "#ffffff", f"{hand_name} grip", 7))
                parts.append(draw_axes(projector, c["grip_p"], c, "grip", 0.90))
            if "aim_p" in c and "aim_f" in c:
                parts.append(svg_dot(projector, c["aim_p"], "#fde68a", f"{hand_name} aim", 5))
                parts.append(svg_line(projector, c["aim_p"], add(c["aim_p"], mul(c["aim_f"], 0.85)), "#eab308", 5, label=f"{hand_name} aim ray"))
            if "target_p" in c:
                parts.append(svg_dot(projector, c["target_p"], "#06b6d4", f"{hand_name} hand target", 6))
                parts.append(draw_axes(projector, c["target_p"], c, "target", 0.75))
        for m, color, label in ((support, "#f97316", "support"), (terminal, "#a855f7", "terminal")):
            if not m:
                continue
            if "solved_p" in m:
                parts.append(svg_dot(projector, m["solved_p"], color, f"{hand_name} {label} {m.get('anchor', 'joint')}", 6))
            if "target_p" in m and "solved_p" in m:
                delta = length(sub(m["solved_p"], m["target_p"]))
                parts.append(svg_line(projector, m["target_p"], m["solved_p"], "#db2777", max(2, min(12, 2 + delta * 60)), label=f"{hand_name} {label} delta {delta:.5f}m"))
            if "target_p" in m:
                parts.append(draw_axes(projector, m["target_p"], m, "target", 0.35 if label == "support" else 0.45))
            if "solved_p" in m:
                solved_axes = {
                    "solved_i": m.get("solved_i"),
                    "solved_j": m.get("solved_j"),
                    "solved_k": m.get("solved_k"),
                }
                parts.append(draw_axes(projector, m["solved_p"], solved_axes, "solved", 0.65 if label == "support" else 0.80))
        if h:
            if "hardpoint_o" in h:
                parts.append(svg_dot(projector, h["hardpoint_o"], "#10b981", f"{hand_name} {h.get('hardpoint', 'hardpoint')}", 6))
            if "target_o" in h and "hardpoint_o" in h:
                delta = length(sub(h["hardpoint_o"], h["target_o"]))
                parts.append(svg_line(projector, h["target_o"], h["hardpoint_o"], "#059669", max(2, min(12, 2 + delta * 60)), dash="8 5", label=f"{hand_name} hardpoint delta {delta:.5f}m"))

    parts.append("</svg>")
    return "\n".join(parts)


def render_table(controller, mesh, hardpoint):
    rows = []
    mesh_hands = {key[0] for key in mesh.keys()}
    for hand in sorted(set(controller.keys()) | mesh_hands):
        hand_name = "left" if hand == 0 else "right"
        c = controller.get(hand, {})
        role_records = [mesh[key] for key in sorted(mesh.keys()) if key[0] == hand]
        if not role_records:
            role_records = [{}]
        for m in role_records:
            aim_target_error = None
            grip_target_error = None
            forward_error = c.get("handForwardMinusRayForwardMag")
            if c.get("aim_p") and c.get("target_p"):
                aim_target_error = length(sub(c["target_p"], c["aim_p"]))
            if c.get("grip_p") and c.get("target_p"):
                grip_target_error = length(sub(c["target_p"], c["grip_p"]))
            if forward_error is None and c.get("aim_f") and c.get("handForward_p"):
                forward_error = length(sub(c["handForward_p"], c["aim_f"]))
            mesh_error = m.get("deltaMag")
            controller_pivot_error = c.get("handMinusRayOriginMag", c.get("targetMinusAimMag", aim_target_error))
            h = hardpoint.get(hand, {})
            hardpoint_error = h.get("renderedHardpointMinusHandPivotMag", h.get("hardpointMinusTargetMag"))
            contract_ok = c.get("targetPose") == TARGET_CONTRACT
            verdict = "PASS" if (
                contract_ok and
                controller_pivot_error is not None and controller_pivot_error <= 0.005 and
                forward_error is not None and forward_error <= 0.005 and
                mesh_error is not None and mesh_error <= 0.005 and
                hardpoint_error is not None and hardpoint_error <= 0.010 and
                h.get("correctionEnabled") == "true"
            ) else "CHECK"
            cells = [
                hand_name,
                html.escape(c.get("sample", "-")),
                html.escape(c.get("flags", "-")),
                html.escape(c.get("targetPose", "-")),
                fmt(c["grip_p"]) if c.get("grip_p") else "-",
                fmt(c["aim_p"]) if c.get("aim_p") else "-",
                fmt(c["aim_f"]) if c.get("aim_f") else "-",
                f"{controller_pivot_error:.6f} m" if controller_pivot_error is not None else "-",
                f"{forward_error:.6f}" if forward_error is not None else "-",
                f"{grip_target_error:.6f} m" if grip_target_error is not None else "-",
                html.escape(m.get("role", "-")),
                html.escape(m.get("anchor", "-")),
                f"{mesh_error:.6f} m" if mesh_error is not None else "-",
                html.escape(h.get("hardpoint", "-")),
                f"{hardpoint_error:.6f} m" if hardpoint_error is not None else "-",
                html.escape(h.get("correctionEnabled", "-")),
            ]
            row = "<tr>" + "".join(f"<td>{cell}</td>" for cell in cells) + f"<td class=\"{verdict.lower()}\">{verdict}</td></tr>"
            rows.append(row)
    return "\n".join(rows)


def render_html(trace_path, output_path, controller, mesh, hardpoint):
    svg = render_svg(controller, mesh, hardpoint)
    rows = render_table(controller, mesh, hardpoint)
    missing = "" if rows else "<p class=\"warn\">No SWGVRHandsProof records found yet. Run the VR client once with hand trace enabled, then run this renderer again.</p>"
    body = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>SWG VR Hand Math Proof</title>
<style>
body {{ margin: 0; font-family: Segoe UI, Arial, sans-serif; background: #e5e7eb; color: #111827; }}
main {{ max-width: 1200px; margin: 0 auto; padding: 28px; }}
section {{ background: white; border: 1px solid #cbd5e1; border-radius: 8px; padding: 20px; margin-bottom: 18px; }}
h1 {{ margin: 0 0 8px; font-size: 28px; }}
h2 {{ margin: 0 0 10px; font-size: 20px; }}
code {{ background: #f1f5f9; padding: 2px 5px; border-radius: 4px; }}
table {{ width: 100%; border-collapse: collapse; font-size: 13px; }}
th, td {{ border-bottom: 1px solid #e5e7eb; padding: 8px; text-align: left; vertical-align: top; }}
th {{ background: #f8fafc; }}
.pass {{ color: #15803d; font-weight: 700; }}
.check {{ color: #b45309; font-weight: 700; }}
.warn {{ color: #b45309; font-weight: 700; }}
</style>
</head>
<body>
<main>
<section>
<h1>SWG VR Hand Math Proof</h1>
<p>Input trace: <code>{html.escape(str(trace_path))}</code></p>
<p>Generated file: <code>{html.escape(str(output_path))}</code></p>
</section>
<section>
<h2>Paper Math</h2>
<p><code>T_grip_swg = Camera_world * VrToSwg(OpenXR_grip)</code></p>
<p><code>T_aim_swg = Camera_world * VrToSwg(OpenXR_aim)</code></p>
<p><code>T_hand_target.position = T_aim_swg.position</code>, <code>-T_hand_target.K = -T_aim_swg.K</code>, and projected grip <code>I</code> supplies roll.</p>
<p>The SWG glove bind point is solved by <code>T_anchor = T_hand_target * inverse(T_anchor_to_bind)</code>, so the mesh bind pivot lands exactly on the controller ray origin and the hand forward axis matches the ray.</p>
<p>Required errors: <code>|target.position - aim.position|</code> near zero, <code>|hand.forward - aim.forward|</code> near zero, solved mesh bind delta near zero, and final rendered <code>hold_l/hold_r</code> hardpoint delta near zero.</p>
</section>
<section>
<h2>3D Drawing</h2>
{missing}
{svg}
</section>
<section>
<h2>Numbers</h2>
<table>
<thead><tr><th>Hand</th><th>Sample</th><th>Flags</th><th>Target Contract</th><th>Grip XYZ</th><th>Aim XYZ</th><th>Aim Dir</th><th>Aim Target Error</th><th>Forward Error</th><th>Grip Position Offset</th><th>Mesh Role</th><th>Mesh Anchor</th><th>Mesh Error</th><th>Hardpoint</th><th>Hardpoint Error</th><th>Correction</th><th>Verdict</th></tr></thead>
<tbody>{rows}</tbody>
</table>
</section>
</main>
</body>
</html>
"""
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(body, encoding="utf-8")


def main():
    repo_root = Path(__file__).resolve().parents[2]
    workspace_root = repo_root.parent.parent if repo_root.parent.name.lower() == "source" else repo_root.parent
    proof_root = workspace_root / "proofs"
    parser = argparse.ArgumentParser(description="Render SWG VR hand/controller math proof from hand trace.")
    parser.add_argument("--trace", default=str(proof_root / "og_vr_hands_trace.log"))
    parser.add_argument("--out", default=str(proof_root / "og_vr_hand_math_proof.html"))
    args = parser.parse_args()

    trace_path = Path(args.trace)
    output_path = Path(args.out)
    controller, mesh, hardpoint = latest_records(trace_path)
    render_html(trace_path, output_path, controller, mesh, hardpoint)
    print(output_path)


if __name__ == "__main__":
    main()
