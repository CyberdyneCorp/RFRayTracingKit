// Result value types. Each reads its data out of a C result handle into Swift
// arrays/values; the C handle is freed by the caller (Simulator) immediately
// after, so these are self-contained copies with no lifetime coupling.
import CRFTrace

public struct ReceiverResult {
    public let id: String
    public let position: Vec3
    public let hasSignal: Bool
    public let receivedPowerDbm: Double
    public let pathLossDb: Double
    public let delaySpreadNs: Double
    public let sinrDb: Double
}

public struct RFResult {
    public let frequencyHz: Double
    public let receivers: [ReceiverResult]

    init(reading r: OpaquePointer) throws {
        var freq = 0.0
        try check(rftrace_result_frequency_hz(r, &freq))
        frequencyHz = freq

        var count = 0
        try check(rftrace_result_receiver_count(r, &count))
        var out = [ReceiverResult]()
        out.reserveCapacity(count)
        for i in 0..<count {
            var pos = rftrace_vec3()
            try check(rftrace_result_receiver_position(r, i, &pos))
            var sig: Int32 = 0
            try check(rftrace_result_receiver_has_signal(r, i, &sig))
            var power = 0.0
            try check(rftrace_result_receiver_received_power_dbm(r, i, &power))
            var pathLoss = 0.0
            try check(rftrace_result_receiver_path_loss_db(r, i, &pathLoss))
            var delay = 0.0
            try check(rftrace_result_receiver_delay_spread_ns(r, i, &delay))
            var sinr = 0.0
            try check(rftrace_result_receiver_sinr_db(r, i, &sinr))
            out.append(ReceiverResult(id: try readReceiverId(r, i),
                                      position: Vec3(pos),
                                      hasSignal: sig != 0,
                                      receivedPowerDbm: power,
                                      pathLossDb: pathLoss,
                                      delaySpreadNs: delay,
                                      sinrDb: sinr))
        }
        receivers = out
    }
}

public struct CoverageResult {
    public let cols: Int32
    public let rows: Int32
    public let powerDbm: [Double]     // row-major, size rows*cols
    public let pathLossDb: [Double]

    init(reading c: OpaquePointer) throws {
        var cc: Int32 = 0, rr: Int32 = 0
        try check(rftrace_coverage_dimensions(c, &cc, &rr))
        cols = cc; rows = rr

        var n = 0
        try check(rftrace_coverage_cell_count(c, &n))
        var power = [Double](repeating: 0, count: n)
        var written = 0
        try check(rftrace_coverage_power_dbm(c, &power, n, &written))
        var loss = [Double](repeating: 0, count: n)
        try check(rftrace_coverage_path_loss_db(c, &loss, n, &written))
        powerDbm = power
        pathLossDb = loss
    }

    public func powerAt(row: Int, col: Int) -> Double { powerDbm[row * Int(cols) + col] }
}

public struct RouteSample {
    public let position: Vec3
    public let hasSignal: Bool
    public let receivedPowerDbm: Double
    public let distanceMeters: Double
}

public struct RouteResult {
    public let samples: [RouteSample]

    init(reading r: OpaquePointer) throws {
        var n = 0
        try check(rftrace_route_result_sample_count(r, &n))
        var powers = [Double](repeating: 0, count: n)
        var written = 0
        try check(rftrace_route_result_powers(r, &powers, n, &written))
        var distances = [Double](repeating: 0, count: n)
        try check(rftrace_route_result_distances(r, &distances, n, &written))
        var out = [RouteSample]()
        out.reserveCapacity(n)
        for i in 0..<n {
            var pos = rftrace_vec3()
            try check(rftrace_route_result_sample_position(r, i, &pos))
            var sig: Int32 = 0
            try check(rftrace_route_result_sample_has_signal(r, i, &sig))
            out.append(RouteSample(position: Vec3(pos), hasSignal: sig != 0,
                                   receivedPowerDbm: powers[i], distanceMeters: distances[i]))
        }
        samples = out
    }
}

/// Read a receiver id via count-then-fill, growing the buffer on truncation.
private func readReceiverId(_ r: OpaquePointer, _ i: Int) throws -> String {
    var cap = 64
    while true {
        var buf = [CChar](repeating: 0, count: cap)
        var written = 0
        let status = rftrace_result_receiver_id(r, i, &buf, cap, &written)
        if status == RFTRACE_OK { return String(cString: buf) }
        if status == RFTRACE_TRUNCATED, cap < 65_536 { cap *= 2; continue }
        try check(status)  // throws for any other status
    }
}
