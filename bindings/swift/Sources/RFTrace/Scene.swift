// Scene — an RAII Swift wrapper over `rftrace_scene`. Handles are freed in
// `deinit`; callers never touch raw pointers.
import CRFTrace

public final class Scene {
    let handle: OpaquePointer

    public init() throws {
        guard let h = rftrace_scene_create() else {
            throw RFTraceError(status: RFTRACE_ERROR, message: "rftrace_scene_create failed")
        }
        handle = h
    }

    deinit { rftrace_scene_destroy(handle) }

    /// Add (or replace by name) a material; returns its index.
    @discardableResult
    public func addMaterial(_ m: Material) throws -> Int32 {
        var index: Int32 = -1
        try m.name.withCString { namePtr in
            var cm = rftrace_material(name: namePtr,
                                      relative_permittivity: m.relativePermittivity,
                                      conductivity: m.conductivity,
                                      roughness: m.roughness,
                                      penetration_loss_db: m.penetrationLossDb,
                                      reflection_loss_db: m.reflectionLossDb)
            try check(rftrace_scene_add_material(handle, &cm, &index))
        }
        return index
    }

    /// Append a triangle mesh from a flat array of `triangleCount*9` doubles laid
    /// out `[v0x v0y v0z v1x v1y v1z v2x v2y v2z ...]`. `material` selects a named
    /// material; `nil` uses the default.
    public func addMesh(vertices: [Double], material: String? = nil) throws {
        precondition(vertices.count % 9 == 0, "vertices must be triangleCount*9 doubles")
        let triangleCount = vertices.count / 9
        try vertices.withUnsafeBufferPointer { vp in
            if let material = material {
                try material.withCString { namePtr in
                    try check(rftrace_scene_add_mesh(handle, vp.baseAddress, triangleCount, namePtr))
                }
            } else {
                try check(rftrace_scene_add_mesh(handle, vp.baseAddress, triangleCount, nil))
            }
        }
    }

    /// Convenience: append triangles given as `(v0, v1, v2)` tuples.
    public func addMesh(triangles: [(Vec3, Vec3, Vec3)], material: String? = nil) throws {
        var flat = [Double]()
        flat.reserveCapacity(triangles.count * 9)
        for (a, b, c) in triangles {
            flat.append(contentsOf: [a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z])
        }
        try addMesh(vertices: flat, material: material)
    }

    public func addTransmitter(id: String, position: Vec3, frequencyHz: Double, powerDbm: Double) throws {
        try id.withCString { idPtr in
            try check(rftrace_scene_add_transmitter(handle, idPtr, position.c, frequencyHz, powerDbm))
        }
    }

    public func addReceiver(id: String, position: Vec3) throws {
        try id.withCString { idPtr in
            try check(rftrace_scene_add_receiver(handle, idPtr, position.c))
        }
    }
}
