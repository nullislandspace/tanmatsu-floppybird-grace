#!/usr/bin/env python3
"""Generate a PNG icon of the floppy disk character from Floppy Bird."""

from PIL import Image, ImageDraw

SIZE = 512
PADDING = 60


def draw_floppy_icon(size=SIZE):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # The floppy in-game is 30x36 (5:6 ratio). Scale up to fill the icon.
    # Leave room for wings on the sides.
    body_w = int(size * 0.50)
    body_h = int(body_w * 36 / 30)
    cx = size // 2
    cy = size // 2

    body_left = cx - body_w // 2
    body_top = cy - body_h // 2
    body_right = body_left + body_w
    body_bottom = body_top + body_h

    wing_extent = int(body_w * 0.45)
    wing_half_h = int(body_h * 0.17)
    wing_cy = cy - int(body_h * 0.02)

    # Wings (behind body)
    wing_color = (170, 170, 170, 255)
    # Left wing
    draw.polygon([
        (body_left, wing_cy - wing_half_h),
        (body_left, wing_cy + wing_half_h),
        (body_left - wing_extent, wing_cy - int(wing_half_h * 0.6)),
    ], fill=wing_color)
    # Right wing
    draw.polygon([
        (body_right, wing_cy - wing_half_h),
        (body_right, wing_cy + wing_half_h),
        (body_right + wing_extent, wing_cy - int(wing_half_h * 0.6)),
    ], fill=wing_color)

    # Body
    body_color = (34, 34, 34, 255)
    corner = int(body_w * 0.06)
    draw.rounded_rectangle(
        [body_left, body_top, body_right, body_bottom],
        radius=corner, fill=body_color,
    )

    # Label (blue rectangle on upper portion)
    label_margin = int(body_w * 0.10)
    label_top = body_top + label_margin
    label_h = int(body_h * 0.44)
    label_color = (65, 105, 225, 255)
    draw.rounded_rectangle(
        [body_left + label_margin, label_top,
         body_right - label_margin, label_top + label_h],
        radius=corner // 2, fill=label_color,
    )

    # White text lines on label
    line_margin = int(body_w * 0.16)
    line_h = int(body_h * 0.035)
    line_gap = int(body_h * 0.06)
    line_y = label_top + int(label_h * 0.25)
    white = (255, 255, 255, 255)
    for _ in range(3):
        draw.rectangle(
            [body_left + line_margin, line_y,
             body_right - line_margin, line_y + line_h],
            fill=white,
        )
        line_y += line_h + line_gap

    # Metal slider at top
    slider_w = int(body_w * 0.50)
    slider_h = int(body_h * 0.07)
    slider_color = (192, 192, 192, 255)
    draw.rounded_rectangle(
        [cx - slider_w // 2, body_top,
         cx + slider_w // 2, body_top + slider_h],
        radius=corner // 2, fill=slider_color,
    )

    # Hub/spindle hole (circle in lower area)
    hub_r = int(body_w * 0.12)
    hub_cy = body_top + int(body_h * 0.74)
    hub_color = (68, 68, 68, 255)
    draw.ellipse(
        [cx - hub_r, hub_cy - hub_r, cx + hub_r, hub_cy + hub_r],
        fill=hub_color,
    )
    inner_r = int(hub_r * 0.6)
    draw.ellipse(
        [cx - inner_r, hub_cy - inner_r, cx + inner_r, hub_cy + inner_r],
        fill=body_color,
    )

    # Write-protect notch (top-right)
    notch_w = int(body_w * 0.10)
    notch_h = int(body_h * 0.12)
    notch_color = (68, 68, 68, 255)
    draw.rectangle(
        [body_right - notch_w, body_top,
         body_right, body_top + notch_h],
        fill=notch_color,
    )

    return img


if __name__ == "__main__":
    img = draw_floppy_icon(512)
    img.save("floppy_icon.png")
    print("Generated floppy_icon.png (512x512)")
