import XCTest
@testable import RFTrace

// Mirrors tests/c_api_test.c: an unobstructed LOS link's received power must be
// tx power minus free-space path loss. Runnable only where a Swift toolchain and
// librftrace_c are available (see the package README).
final class RFTraceTests: XCTestCase {
    func testVersion() {
        XCTAssertFalse(rftraceVersion.isEmpty)
        XCTAssertEqual(rftraceABIVersion, 1)
    }

    func testLineOfSightPower() throws {
        let scene = try Scene()
        try scene.addTransmitter(id: "tx", position: Vec3(0, 0, 0),
                                 frequencyHz: 3.5e9, powerDbm: 43.0)
        try scene.addReceiver(id: "rx", position: Vec3(100, 0, 0))

        var settings = Settings()
        settings.maxReflections = 0  // LOS only
        let sim = try Simulator(settings)
        let result = try sim.run(scene)

        XCTAssertEqual(result.receivers.count, 1)
        let rx = result.receivers[0]
        XCTAssertTrue(rx.hasSignal)

        // Free-space path loss at 100 m, 3.5 GHz:
        //   FSPL = 20 log10(4π d f / c)
        let c = 299_792_458.0
        let fspl = 20.0 * log10(4.0 * Double.pi * 100.0 * 3.5e9 / c)
        XCTAssertEqual(rx.receivedPowerDbm, 43.0 - fspl, accuracy: 1e-6)
    }

    func testCoverageRunsAndAgrees() throws {
        let scene = try Scene()
        try scene.addTransmitter(id: "tx", position: Vec3(50, 50, 30),
                                 frequencyHz: 3.5e9, powerDbm: 43.0)
        var settings = Settings()
        settings.maxReflections = 0
        let sim = try Simulator(settings)

        let grid = Grid(origin: Vec3(0, 0, 0), cellSize: 25.0, cols: 4, rows: 4, height: 1.5)
        let cov = try sim.runCoverage(scene, grid: grid)
        XCTAssertEqual(cov.powerDbm.count, 16)
        XCTAssertTrue(cov.powerDbm.allSatisfy { $0.isFinite })
    }

    func testInvalidUsageThrows() throws {
        // A route with no waypoints is a bad argument -> the C API returns a
        // non-OK status, which surfaces as a thrown RFTraceError.
        let scene = try Scene()
        try scene.addTransmitter(id: "tx", position: Vec3(0, 0, 0),
                                 frequencyHz: 3.5e9, powerDbm: 43.0)
        let sim = try Simulator()
        XCTAssertThrowsError(try sim.runRoute(scene, waypoints: [], sampleSpacing: 5.0)) { error in
            XCTAssertTrue(error is RFTraceError)
        }
    }
}
