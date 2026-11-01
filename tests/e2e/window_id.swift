import CoreGraphics
import Foundation

let owner = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "OBS Studio"
let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID) as? [[String: Any]] ?? []
let candidates = windows.compactMap { window -> (id: Int, area: Double)? in
    guard window[kCGWindowOwnerName as String] as? String == owner,
          window[kCGWindowLayer as String] as? Int == 0,
          let id = window[kCGWindowNumber as String] as? Int,
          let bounds = window[kCGWindowBounds as String] as? [String: Double] else { return nil }
    return (id, (bounds["Width"] ?? 0) * (bounds["Height"] ?? 0))
}
guard let largest = candidates.max(by: { $0.area < $1.area }) else { exit(1) }
print(largest.id)
