import AppKit

let destination = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
let sprite = [
    " ddd      ddd    ", " dodd    dodd    ", " doooddddoood    ", " dooooooooood    ",
    "doooooooooood   ", "dooooooooooood  ", "dooodooodooood  ", "dooodooodooood  ",
    "doooooooooodd   ", " doooopooood    ", "  doooooood     ", "   doooood      ",
    "  doooooood     ", "  doooooood     ", " dooooooooood   ", " doooooooooood  ",
    " doooooooooood  ", " doooddooodoodd ", "  ddd  ddd dddd ", "                "
]
for (points, scale) in [(16, 1), (16, 2), (32, 1), (32, 2), (128, 1), (128, 2), (256, 1), (256, 2), (512, 1), (512, 2)] {
    let pixels = points * scale
    let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: pixels, pixelsHigh: pixels,
        bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
        colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
    let unit = CGFloat(pixels) / 64
    NSColor(calibratedRed: 0.94, green: 0.87, blue: 0.74, alpha: 1).setFill()
    NSBezierPath(roundedRect: NSRect(x: 3 * unit, y: 3 * unit, width: 58 * unit, height: 58 * unit),
                 xRadius: 12 * unit, yRadius: 12 * unit).fill()
    for (left, fur) in [(12, NSColor(calibratedRed: 0.85, green: 0.47, blue: 0.20, alpha: 1)),
                        (35, NSColor(calibratedRed: 0.20, green: 0.23, blue: 0.28, alpha: 1))] {
        for (row, line) in sprite.enumerated() {
            for (column, cell) in line.enumerated() {
                guard cell != " " else { continue }
                if cell == "o" { fur.setFill() }
                else if cell == "p" { NSColor(calibratedRed: 0.91, green: 0.62, blue: 0.59, alpha: 1).setFill() }
                else { NSColor(calibratedRed: 0.16, green: 0.14, blue: 0.15, alpha: 1).setFill() }
                NSRect(x: CGFloat(left + column) * unit, y: CGFloat(38 - row) * unit,
                       width: unit, height: unit).fill()
            }
        }
    }
    NSColor(calibratedRed: 0.72, green: 0.29, blue: 0.32, alpha: 1).setFill()
    for (x, y, width) in [(28, 47, 3), (32, 47, 3), (27, 46, 9), (28, 45, 7), (29, 44, 5), (30, 43, 3), (31, 42, 1)] {
        NSRect(x: CGFloat(x) * unit, y: CGFloat(y) * unit, width: CGFloat(width) * unit, height: unit).fill()
    }
    NSGraphicsContext.restoreGraphicsState()
    let name = "icon_\(points)x\(points)\(scale == 2 ? "@2x" : "").png"
    try bitmap.representation(using: .png, properties: [:])!.write(to: destination.appendingPathComponent(name))
}
