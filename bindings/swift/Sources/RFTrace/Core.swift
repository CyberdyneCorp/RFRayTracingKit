// Idiomatic Swift core types for RFTraceKit: errors, geometry, enums, and the
// simulation settings/grid, converting to/from the C ABI (CRFTrace).
//
// UNVERIFIED: authored against include/rftrace/rftrace.h; not compiled here (no
// Swift toolchain on the development host). Build where Swift + librftrace_c exist.
import CRFTrace

/// Error thrown when a C API call returns a non-OK status. Carries the C
/// `rftrace_last_error()` message.
public struct RFTraceError: Error, CustomStringConvertible {
    public let status: rftrace_status
    public let message: String
    public var description: String { "RFTraceError(\(status.rawValue)): \(message)" }
}

/// Throw an `RFTraceError` (with the thread-local last-error message) unless the
/// status is OK.
@inline(__always)
func check(_ status: rftrace_status) throws {
    guard status == RFTRACE_OK else {
        throw RFTraceError(status: status, message: String(cString: rftrace_last_error()))
    }
}

/// Library semantic version (e.g. "0.1.0").
public var rftraceVersion: String { String(cString: rftrace_version()) }
/// Numeric ABI version of the linked `librftrace_c`.
public var rftraceABIVersion: Int32 { rftrace_abi_version() }

// MARK: - Geometry

public struct Vec3: Equatable {
    public var x, y, z: Double
    public init(_ x: Double, _ y: Double, _ z: Double) { self.x = x; self.y = y; self.z = z }
    init(_ c: rftrace_vec3) { self.init(c.x, c.y, c.z) }
    var c: rftrace_vec3 { rftrace_vec3(x: x, y: y, z: z) }
}

/// A scene material (electrical + loss parameters). Mirrors `rftrace_material`.
public struct Material {
    public var name: String
    public var relativePermittivity: Double
    public var conductivity: Double
    public var roughness: Double
    public var penetrationLossDb: Double
    public var reflectionLossDb: Double
    public init(name: String, relativePermittivity: Double = 1.0, conductivity: Double = 0.0,
                roughness: Double = 0.0, penetrationLossDb: Double = 0.0,
                reflectionLossDb: Double = 0.0) {
        self.name = name
        self.relativePermittivity = relativePermittivity
        self.conductivity = conductivity
        self.roughness = roughness
        self.penetrationLossDb = penetrationLossDb
        self.reflectionLossDb = reflectionLossDb
    }
}

// MARK: - Enums (raw values mirror the C #defines / C++ enum order)

public enum Backend: Int32 { case cpu = 0, embree = 1, metal = 2, cuda = 3, opencl = 4 }
public enum PropagationMode: Int32 { case imageMethod = 0, rayLaunch = 1 }
public enum DiffractionModel: Int32 { case singleEdge = 0, bullington = 1, deygout = 2, utd = 3 }

// MARK: - Settings

/// Simulation settings. Mirrors `rftrace_settings`; defaults match the C++
/// `SimulationSettings{}` (via `rftrace_settings_default`).
public struct Settings {
    public var backend: Backend = .cpu
    public var mode: PropagationMode = .imageMethod
    public var maxReflections: Int32 = 1
    public var raysPerTransmitter: Int32 = 100_000
    public var captureRadius: Double = 2.0
    public var powerFloorDbm: Double = -160.0
    public var seed: UInt64 = 1
    public var coherent: Bool = false
    public var allowBackendFallback: Bool = true
    public var threadCount: Int32 = 0
    public var simulationId: String = "rftrace_sim"
    public var enableDiffraction: Bool = false
    public var diffractionModel: DiffractionModel = .singleEdge
    public var enableDepolarization: Bool = false
    public var enableRain: Bool = false
    public var rainRateMmPerHr: Double = 0.0
    public var enableGaseousAttenuation: Bool = false
    public var enableVegetation: Bool = false
    public var enableSinr: Bool = false
    public var noiseBandwidthHz: Double = 20e6
    public var noiseFigureDb: Double = 7.0
    public var noiseFloorDbmOverride: Double = .nan

    public init() {}

    /// Build the C struct, starting from the library defaults so ABI growth
    /// (new appended fields) stays source-compatible, then applying overrides.
    func toC() -> rftrace_settings {
        var s = rftrace_settings()
        rftrace_settings_default(&s)
        s.backend = backend.rawValue
        s.mode = mode.rawValue
        s.max_reflections = maxReflections
        s.rays_per_transmitter = raysPerTransmitter
        s.capture_radius = captureRadius
        s.power_floor_dbm = powerFloorDbm
        s.seed = seed
        s.coherent = coherent ? 1 : 0
        s.allow_backend_fallback = allowBackendFallback ? 1 : 0
        s.thread_count = threadCount
        s.enable_diffraction = enableDiffraction ? 1 : 0
        s.diffraction_model = diffractionModel.rawValue
        s.enable_depolarization = enableDepolarization ? 1 : 0
        s.enable_rain = enableRain ? 1 : 0
        s.rain_rate_mm_per_hr = rainRateMmPerHr
        s.enable_gaseous_attenuation = enableGaseousAttenuation ? 1 : 0
        s.enable_vegetation = enableVegetation ? 1 : 0
        s.enable_sinr = enableSinr ? 1 : 0
        s.noise_bandwidth_hz = noiseBandwidthHz
        s.noise_figure_db = noiseFigureDb
        s.noise_floor_dbm_override = noiseFloorDbmOverride
        // simulation_id is a fixed C char[64]; copy up to 63 bytes + NUL.
        withUnsafeMutablePointer(to: &s.simulation_id) { tuplePtr in
            tuplePtr.withMemoryRebound(to: CChar.self, capacity: 64) { dst in
                let bytes = simulationId.utf8CString
                let n = min(bytes.count, 64)
                for i in 0..<n { dst[i] = bytes[i] }
                dst[63] = 0
            }
        }
        return s
    }
}

/// A coverage grid. Mirrors `rftrace_grid`.
public struct Grid {
    public var origin: Vec3
    public var cellSize: Double
    public var cols: Int32
    public var rows: Int32
    public var height: Double
    public init(origin: Vec3 = Vec3(0, 0, 0), cellSize: Double = 2.0,
                cols: Int32 = 1, rows: Int32 = 1, height: Double = 1.5) {
        self.origin = origin
        self.cellSize = cellSize
        self.cols = cols
        self.rows = rows
        self.height = height
    }
    var c: rftrace_grid {
        rftrace_grid(origin: origin.c, cell_size: cellSize, cols: cols, rows: rows, height: height)
    }
}
