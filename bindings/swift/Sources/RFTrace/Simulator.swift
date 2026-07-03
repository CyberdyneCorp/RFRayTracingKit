// Simulator — an RAII Swift wrapper over `rftrace_simulator`. Each run drives the
// C API, reads the result into a Swift value type, and frees the C result handle
// immediately (via `defer`), so callers deal only with value types and `throws`.
import CRFTrace

public final class Simulator {
    let handle: OpaquePointer

    public init(_ settings: Settings = Settings()) throws {
        var c = settings.toC()
        guard let h = rftrace_simulator_create(&c) else {
            throw RFTraceError(status: RFTRACE_ERROR, message: "rftrace_simulator_create failed")
        }
        handle = h
    }

    deinit { rftrace_simulator_destroy(handle) }

    /// Simulate every (transmitter, receiver) pair.
    public func run(_ scene: Scene) throws -> RFResult {
        var out: OpaquePointer?
        try check(rftrace_simulator_run(handle, scene.handle, &out))
        guard let r = out else {
            throw RFTraceError(status: RFTRACE_ERROR, message: "run returned no result")
        }
        defer { rftrace_result_free(r) }
        return try RFResult(reading: r)
    }

    /// Evaluate received power / path loss over a coverage grid.
    public func runCoverage(_ scene: Scene, grid: Grid) throws -> CoverageResult {
        var g = grid.c
        var out: OpaquePointer?
        try check(rftrace_simulator_run_coverage(handle, scene.handle, &g, &out))
        guard let c = out else {
            throw RFTraceError(status: RFTRACE_ERROR, message: "runCoverage returned no result")
        }
        defer { rftrace_coverage_free(c) }
        return try CoverageResult(reading: c)
    }

    /// Simulate a moving receiver along a polyline of `waypoints`.
    public func runRoute(_ scene: Scene, waypoints: [Vec3], sampleSpacing: Double,
                         speedMps: Double = 0) throws -> RouteResult {
        let cWaypoints = waypoints.map { $0.c }
        var out: OpaquePointer?
        try cWaypoints.withUnsafeBufferPointer { wp in
            try check(rftrace_simulator_run_route(handle, scene.handle, wp.baseAddress,
                                                  wp.count, sampleSpacing, speedMps, &out))
        }
        guard let r = out else {
            throw RFTraceError(status: RFTRACE_ERROR, message: "runRoute returned no result")
        }
        defer { rftrace_route_result_free(r) }
        return try RouteResult(reading: r)
    }
}
