// Python bindings for RFTraceKit — the compiled extension `rftracekit._native`.
//
// This is the only file that couples the C++ core to Python. It exposes the
// public API declared in include/rftrace/*.hpp with snake_case names, accepts
// Python lists/tuples anywhere a Vec3 is expected, and returns geometry buffers
// as NumPy arrays. Higher-level ergonomics live in the pure-Python package.

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

#include "rftrace/rftrace.hpp"

namespace py = pybind11;
using namespace rftrace;

namespace {

// ---- Vec3 <-> Python helpers ------------------------------------------------

/// Convert any Python sequence (list/tuple/ndarray) of 3 numbers into a Vec3.
Vec3 toVec3(const py::handle& obj) {
  auto values = obj.cast<std::vector<double>>();
  if (values.size() != 3) {
    throw std::invalid_argument(
        "expected a sequence of 3 numbers for a 3D vector, got " +
        std::to_string(values.size()));
  }
  return Vec3(values[0], values[1], values[2]);
}

/// Return a Vec3 as a NumPy float64 array of shape (3,).
py::array_t<double> vec3ToArray(const Vec3& v) {
  py::array_t<double> arr(3);
  auto r = arr.mutable_unchecked<1>();
  r(0) = v.x();
  r(1) = v.y();
  r(2) = v.z();
  return arr;
}

/// Return a list of points as a NumPy float64 array of shape (N, 3).
py::array_t<double> pointsToArray(const std::vector<Vec3>& pts) {
  py::array_t<double> arr(
      {static_cast<py::ssize_t>(pts.size()), static_cast<py::ssize_t>(3)});
  auto r = arr.mutable_unchecked<2>();
  for (py::ssize_t i = 0; i < static_cast<py::ssize_t>(pts.size()); ++i) {
    r(i, 0) = pts[i].x();
    r(i, 1) = pts[i].y();
    r(i, 2) = pts[i].z();
  }
  return arr;
}

/// Return a std::vector<double> as a 1D NumPy array (copy).
py::array_t<double> toArray1d(const std::vector<double>& data) {
  py::array_t<double> arr(static_cast<py::ssize_t>(data.size()));
  auto r = arr.mutable_unchecked<1>();
  for (py::ssize_t i = 0; i < static_cast<py::ssize_t>(data.size()); ++i) {
    r(i) = data[i];
  }
  return arr;
}

std::string toLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

/// Accept a Backend enum or a string ("cpu", "metal", ...).
Backend backendFromObject(const py::handle& obj) {
  if (py::isinstance<py::str>(obj)) {
    return backendFromString(obj.cast<std::string>());
  }
  return obj.cast<Backend>();
}

/// Parse a propagation-mode string ("image" / "raylaunch", tolerant of casing).
PropagationMode modeFromString(const std::string& name) {
  const std::string s = toLower(name);
  if (s == "image" || s == "imagemethod" || s == "image_method") {
    return PropagationMode::ImageMethod;
  }
  if (s == "raylaunch" || s == "ray_launch" || s == "ray-launch") {
    return PropagationMode::RayLaunch;
  }
  throw std::invalid_argument("unknown propagation mode: " + name);
}

/// Accept a PropagationMode enum or a string.
PropagationMode modeFromObject(const py::handle& obj) {
  if (py::isinstance<py::str>(obj)) {
    return modeFromString(obj.cast<std::string>());
  }
  return obj.cast<PropagationMode>();
}

// ---- Binding sections -------------------------------------------------------

void bindEnums(py::module_& m) {
  py::enum_<Backend>(m, "Backend")
      .value("CPU", Backend::CPU)
      .value("Embree", Backend::Embree)
      .value("Metal", Backend::Metal)
      .value("CUDA", Backend::CUDA)
      .value("OpenCL", Backend::OpenCL);

  py::enum_<PropagationMode>(m, "PropagationMode")
      .value("ImageMethod", PropagationMode::ImageMethod)
      .value("RayLaunch", PropagationMode::RayLaunch);

  py::enum_<PathType>(m, "PathType")
      .value("LOS", PathType::LOS)
      .value("Reflection", PathType::Reflection)
      .value("Diffraction", PathType::Diffraction);

  py::enum_<Polarization>(m, "Polarization")
      .value("Vertical", Polarization::Vertical)
      .value("Horizontal", Polarization::Horizontal)
      .value("RHCP", Polarization::RHCP)
      .value("LHCP", Polarization::LHCP)
      .value("NONE", Polarization::None);

  py::enum_<UpAxis>(m, "UpAxis").value("Z", UpAxis::Z);

  m.def("backend_from_string", &backendFromString, py::arg("name"));
  m.def("backend_to_string",
        [](Backend b) { return toString(b); }, py::arg("backend"));
  m.def("backend_available", &backendAvailable, py::arg("backend"));
}

void bindMaterial(py::module_& m) {
  py::class_<Material>(m, "Material")
      .def(py::init<>())
      .def(py::init([](const std::string& name, double eps, double sigma,
                       double roughness, double penetrationLossDb,
                       double reflectionLossDb) {
             return Material{name,       eps,
                             sigma,      roughness,
                             penetrationLossDb, reflectionLossDb};
           }),
           py::arg("name") = "default", py::arg("relative_permittivity") = 1.0,
           py::arg("conductivity") = 0.0, py::arg("roughness") = 0.0,
           py::arg("penetration_loss_db") = 0.0,
           py::arg("reflection_loss_db") = 0.0)
      .def_readwrite("name", &Material::name)
      .def_readwrite("relative_permittivity", &Material::relativePermittivity)
      .def_readwrite("conductivity", &Material::conductivity)
      .def_readwrite("roughness", &Material::roughness)
      .def_readwrite("penetration_loss_db", &Material::penetrationLossDb)
      .def_readwrite("reflection_loss_db", &Material::reflectionLossDb)
      .def("has_electrical_parameters", &Material::hasElectricalParameters);

  py::module_ materials = m.def_submodule("materials", "Built-in presets.");
  materials.def(
      "preset",
      [](const std::string& name) { return materials::preset(name); },
      py::arg("name"));
  materials.def(
      "has_preset",
      [](const std::string& name) { return materials::hasPreset(name); },
      py::arg("name"));
}

void bindAntenna(py::module_& m) {
  py::class_<AntennaPattern>(m, "AntennaPattern")
      .def(py::init<>())
      .def_static("omnidirectional", &AntennaPattern::Omnidirectional,
                  py::arg("gain_dbi") = 0.0)
      .def_readwrite("omni", &AntennaPattern::omni)
      .def_readwrite("peak_gain_dbi", &AntennaPattern::peakGainDbi)
      .def_property(
          "boresight",
          [](const AntennaPattern& a) { return vec3ToArray(a.boresight); },
          [](AntennaPattern& a, const py::handle& v) { a.boresight = toVec3(v); })
      .def_property(
          "up", [](const AntennaPattern& a) { return vec3ToArray(a.up); },
          [](AntennaPattern& a, const py::handle& v) { a.up = toVec3(v); })
      .def_readwrite("azimuth_cut_db", &AntennaPattern::azimuthCutDb)
      .def("gain_towards", [](const AntennaPattern& a,
                              const py::handle& dir) {
        return a.gainTowards(toVec3(dir));
      });

  // Antenna arrays (Phase 7) + array-factor gain.
  py::class_<rf::AntennaArray>(m, "AntennaArray")
      .def(py::init<>())
      .def_readwrite("frequency_hz", &rf::AntennaArray::frequencyHz)
      .def_readwrite("element_gain_dbi", &rf::AntennaArray::elementGainDbi)
      .def_readwrite("back_lobe_floor_db", &rf::AntennaArray::backLobeFloorDb)
      .def_property(
          "boresight",
          [](const rf::AntennaArray& a) { return vec3ToArray(a.boresight); },
          [](rf::AntennaArray& a, const py::handle& v) { a.boresight = toVec3(v); })
      .def("size", &rf::AntennaArray::size);

  m.def(
      "uniform_linear_array",
      [](std::size_t count, double spacing, double freq, py::object axis,
         double eg) {
        return rf::uniformLinearArray(count, spacing, freq, toVec3(axis), eg);
      },
      py::arg("count"), py::arg("spacing_m"), py::arg("frequency_hz"),
      py::arg("axis") = py::make_tuple(0.0, 1.0, 0.0),
      py::arg("element_gain_dbi") = 0.0);

  m.def(
      "uniform_planar_array",
      [](std::size_t nx, std::size_t ny, double dx, double dy, double freq,
         py::object axisX, py::object axisY, double eg) {
        return rf::uniformPlanarArray(nx, ny, dx, dy, freq, toVec3(axisX),
                                      toVec3(axisY), eg);
      },
      py::arg("nx"), py::arg("ny"), py::arg("dx_m"), py::arg("dy_m"),
      py::arg("frequency_hz"), py::arg("axis_x") = py::make_tuple(1.0, 0.0, 0.0),
      py::arg("axis_y") = py::make_tuple(0.0, 0.0, 1.0),
      py::arg("element_gain_dbi") = 0.0);

  // Array-factor gain (dBi) toward `direction` when steered at `beam_dir`.
  m.def(
      "steered_gain_dbi",
      [](const rf::AntennaArray& arr, py::object beam, py::object dir) {
        return rf::steeredGainDbi(arr, toVec3(beam), toVec3(dir));
      },
      py::arg("array"), py::arg("beam_dir"), py::arg("direction"));
}

void bindGeometry(py::module_& m) {
  py::class_<Triangle>(m, "Triangle")
      .def(py::init<>())
      .def(py::init([](const py::handle& v0, const py::handle& v1,
                       const py::handle& v2) {
             return Triangle{toVec3(v0), toVec3(v1), toVec3(v2)};
           }),
           py::arg("v0"), py::arg("v1"), py::arg("v2"))
      .def_property(
          "v0", [](const Triangle& t) { return vec3ToArray(t.v0); },
          [](Triangle& t, const py::handle& v) { t.v0 = toVec3(v); })
      .def_property(
          "v1", [](const Triangle& t) { return vec3ToArray(t.v1); },
          [](Triangle& t, const py::handle& v) { t.v1 = toVec3(v); })
      .def_property(
          "v2", [](const Triangle& t) { return vec3ToArray(t.v2); },
          [](Triangle& t, const py::handle& v) { t.v2 = toVec3(v); })
      .def("normal", [](const Triangle& t) { return vec3ToArray(t.normal()); })
      .def("centroid",
           [](const Triangle& t) { return vec3ToArray(t.centroid()); });
}

void bindSceneTypes(py::module_& m) {
  py::class_<CoordinateSystem>(m, "CoordinateSystem")
      .def(py::init<>())
      .def_readwrite("up", &CoordinateSystem::up)
      .def_readwrite("units", &CoordinateSystem::units)
      .def_readwrite("georeferenced", &CoordinateSystem::georeferenced);

  py::class_<Transmitter>(m, "Transmitter")
      .def(py::init<>())
      .def(py::init([](const std::string& id, const py::handle& position,
                       double frequencyHz, double powerDbm,
                       const AntennaPattern& antenna, Polarization pol) {
             Transmitter tx;
             tx.id = id;
             tx.position = toVec3(position);
             tx.frequencyHz = frequencyHz;
             tx.powerDbm = powerDbm;
             tx.antenna = antenna;
             tx.polarization = pol;
             return tx;
           }),
           py::arg("id"), py::arg("position"),
           py::arg("frequency_hz") = 3.5e9, py::arg("power_dbm") = 43.0,
           py::arg("antenna") = AntennaPattern::Omnidirectional(),
           py::arg("polarization") = Polarization::Vertical)
      .def_readwrite("id", &Transmitter::id)
      .def_property(
          "position",
          [](const Transmitter& t) { return vec3ToArray(t.position); },
          [](Transmitter& t, const py::handle& v) { t.position = toVec3(v); })
      .def_readwrite("frequency_hz", &Transmitter::frequencyHz)
      .def_readwrite("power_dbm", &Transmitter::powerDbm)
      .def_readwrite("antenna", &Transmitter::antenna)
      .def_readwrite("array", &Transmitter::array)
      .def_property(
          "beam_steering",
          [](const Transmitter& t) { return vec3ToArray(t.beamSteering); },
          [](Transmitter& t, const py::handle& v) { t.beamSteering = toVec3(v); })
      .def_readwrite("polarization", &Transmitter::polarization);

  py::class_<Receiver>(m, "Receiver")
      .def(py::init<>())
      .def(py::init([](const std::string& id, const py::handle& position,
                       const AntennaPattern& antenna, Polarization pol) {
             Receiver rx;
             rx.id = id;
             rx.position = toVec3(position);
             rx.antenna = antenna;
             rx.polarization = pol;
             return rx;
           }),
           py::arg("id"), py::arg("position"),
           py::arg("antenna") = AntennaPattern::Omnidirectional(),
           py::arg("polarization") = Polarization::Vertical)
      .def_readwrite("id", &Receiver::id)
      .def_property(
          "position",
          [](const Receiver& r) { return vec3ToArray(r.position); },
          [](Receiver& r, const py::handle& v) { r.position = toVec3(v); })
      .def_readwrite("antenna", &Receiver::antenna)
      .def_readwrite("array", &Receiver::array)
      .def_property(
          "beam_steering",
          [](const Receiver& r) { return vec3ToArray(r.beamSteering); },
          [](Receiver& r, const py::handle& v) { r.beamSteering = toVec3(v); })
      .def_readwrite("polarization", &Receiver::polarization);
}

void bindScene(py::module_& m) {
  py::register_exception<SceneError>(m, "SceneError");

  py::class_<Scene>(m, "Scene")
      .def(py::init<>())
      .def("add_material", &Scene::addMaterial, py::arg("material"))
      .def("material_index", &Scene::materialIndex, py::arg("name"))
      .def("material", &Scene::material, py::arg("index"),
           py::return_value_policy::reference_internal)
      .def("materials", &Scene::materials,
           py::return_value_policy::reference_internal)
      .def(
          "add_mesh",
          [](Scene& s, const std::vector<Triangle>& tris,
             const std::string& materialName) { s.addMesh(tris, materialName); },
          py::arg("triangles"), py::arg("material") = "")
      .def(
          "add_mesh",
          [](Scene& s, const std::vector<Triangle>& tris, int materialIndex) {
            s.addMesh(tris, materialIndex);
          },
          py::arg("triangles"), py::arg("material_index"))
      .def("add_transmitter", &Scene::addTransmitter, py::arg("transmitter"))
      .def("add_receiver", &Scene::addReceiver, py::arg("receiver"))
      .def("transmitters", &Scene::transmitters,
           py::return_value_policy::reference_internal)
      .def("receivers", &Scene::receivers,
           py::return_value_policy::reference_internal)
      .def("load_mesh", &Scene::loadMesh, py::arg("path"),
           py::arg("material") = "")
      .def("load_materials", &Scene::loadMaterials, py::arg("path"))
      .def(
          "coordinate_system",
          [](Scene& s) -> CoordinateSystem& { return s.coordinateSystem(); },
          py::return_value_policy::reference_internal);
}

void bindSettings(py::module_& m) {
  py::class_<SimulationSettings>(m, "SimulationSettings")
      .def(py::init([](const py::handle& backend, const py::handle& mode,
                       int maxReflections, int raysPerTransmitter,
                       double captureRadius, double powerFloorDbm,
                       std::uint64_t seed, bool coherent,
                       bool allowBackendFallback,
                       const std::string& simulationId) {
             SimulationSettings s;
             s.backend = backendFromObject(backend);
             s.mode = modeFromObject(mode);
             s.maxReflections = maxReflections;
             s.raysPerTransmitter = raysPerTransmitter;
             s.captureRadius = captureRadius;
             s.powerFloorDbm = powerFloorDbm;
             s.seed = seed;
             s.coherent = coherent;
             s.allowBackendFallback = allowBackendFallback;
             s.simulationId = simulationId;
             return s;
           }),
           py::arg("backend") = Backend::CPU,
           py::arg("mode") = PropagationMode::ImageMethod,
           py::arg("max_reflections") = 1,
           py::arg("rays_per_transmitter") = 100000,
           py::arg("capture_radius") = 2.0,
           py::arg("power_floor_dbm") = -160.0, py::arg("seed") = 1,
           py::arg("coherent") = false,
           py::arg("allow_backend_fallback") = true,
           py::arg("simulation_id") = "rftrace_sim")
      .def_property(
          "backend",
          [](const SimulationSettings& s) { return s.backend; },
          [](SimulationSettings& s, const py::handle& b) {
            s.backend = backendFromObject(b);
          })
      .def_property(
          "mode", [](const SimulationSettings& s) { return s.mode; },
          [](SimulationSettings& s, const py::handle& mode) {
            s.mode = modeFromObject(mode);
          })
      .def_readwrite("max_reflections", &SimulationSettings::maxReflections)
      .def_readwrite("rays_per_transmitter",
                     &SimulationSettings::raysPerTransmitter)
      .def_readwrite("capture_radius", &SimulationSettings::captureRadius)
      .def_readwrite("power_floor_dbm", &SimulationSettings::powerFloorDbm)
      .def_readwrite("seed", &SimulationSettings::seed)
      .def_readwrite("coherent", &SimulationSettings::coherent)
      .def_readwrite("allow_backend_fallback",
                     &SimulationSettings::allowBackendFallback)
      // Phase 7 advanced-RF toggles (all default off).
      .def_readwrite("enable_diffraction", &SimulationSettings::enableDiffraction)
      .def_readwrite("enable_rain", &SimulationSettings::enableRain)
      .def_readwrite("rain_rate_mm_per_hr", &SimulationSettings::rainRateMmPerHr)
      .def_readwrite("enable_gaseous_attenuation",
                     &SimulationSettings::enableGaseousAttenuation)
      .def_readwrite("enable_vegetation", &SimulationSettings::enableVegetation)
      .def_readwrite("enable_sinr", &SimulationSettings::enableSinr)
      .def_readwrite("noise_bandwidth_hz", &SimulationSettings::noiseBandwidthHz)
      .def_readwrite("noise_figure_db", &SimulationSettings::noiseFigureDb)
      .def_readwrite("noise_floor_dbm_override",
                     &SimulationSettings::noiseFloorDbmOverride)
      .def_readwrite("simulation_id", &SimulationSettings::simulationId);
}

void bindResults(py::module_& m) {
  py::class_<RFPath>(m, "RFPath")
      .def(py::init<>())
      .def_readonly("transmitter_id", &RFPath::transmitterId)
      .def_readonly("receiver_id", &RFPath::receiverId)
      .def_readonly("type", &RFPath::type)
      .def_property_readonly(
          "points", [](const RFPath& p) { return pointsToArray(p.points); })
      .def_readonly("received_power_dbm", &RFPath::receivedPowerDbm)
      .def_readonly("path_loss_db", &RFPath::pathLossDb)
      .def_readonly("phase_rad", &RFPath::phaseRad)
      .def_readonly("delay_seconds", &RFPath::delaySeconds)
      .def_readonly("reflections", &RFPath::reflections)
      .def_readonly("diffractions", &RFPath::diffractions)
      .def_readonly("material_hits", &RFPath::materialHits);

  py::class_<ReceiverResult>(m, "ReceiverResult")
      .def(py::init<>())
      .def_readonly("receiver_id", &ReceiverResult::receiverId)
      .def_property_readonly(
          "position",
          [](const ReceiverResult& r) { return vec3ToArray(r.position); })
      .def_readonly("has_signal", &ReceiverResult::hasSignal)
      .def_readonly("received_power_dbm", &ReceiverResult::receivedPowerDbm)
      .def_readonly("path_loss_db", &ReceiverResult::pathLossDb)
      .def_readonly("delay_spread_ns", &ReceiverResult::delaySpreadNs)
      .def_readonly("phase_rad", &ReceiverResult::phaseRad)
      .def_readonly("serving_transmitter_id",
                    &ReceiverResult::servingTransmitterId)
      .def_readonly("sinr_db", &ReceiverResult::sinrDb)
      .def_readonly("interference_power_dbm",
                    &ReceiverResult::interferencePowerDbm)
      .def_readonly("paths", &ReceiverResult::paths);

  py::class_<TransmitterInfo>(m, "TransmitterInfo")
      .def(py::init<>())
      .def_readonly("id", &TransmitterInfo::id)
      .def_property_readonly(
          "position",
          [](const TransmitterInfo& t) { return vec3ToArray(t.position); })
      .def_readonly("frequency_hz", &TransmitterInfo::frequencyHz)
      .def_readonly("power_dbm", &TransmitterInfo::powerDbm);

  py::class_<RFResult>(m, "RFResult")
      .def(py::init<>())
      .def_readonly("simulation_id", &RFResult::simulationId)
      .def_readonly("frequency_hz", &RFResult::frequencyHz)
      .def_readonly("transmitters", &RFResult::transmitters)
      .def_readonly("receivers", &RFResult::receivers)
      .def(
          "receiver",
          [](const RFResult& r, const std::string& id) -> py::object {
            const ReceiverResult* rx = r.receiver(id);
            if (rx == nullptr) return py::none();
            return py::cast(*rx);
          },
          py::arg("id"));
}

void bindCoverage(py::module_& m) {
  py::class_<CoverageGrid>(m, "CoverageGrid")
      .def(py::init([](const py::handle& origin, double cellSize, int cols,
                       int rows, double height) {
             CoverageGrid g;
             g.origin = toVec3(origin);
             g.cellSize = cellSize;
             g.cols = cols;
             g.rows = rows;
             g.height = height;
             return g;
           }),
           py::arg("origin") = py::make_tuple(0.0, 0.0, 0.0),
           py::arg("cell_size") = 2.0, py::arg("cols") = 1, py::arg("rows") = 1,
           py::arg("height") = 1.5)
      .def_property(
          "origin", [](const CoverageGrid& g) { return vec3ToArray(g.origin); },
          [](CoverageGrid& g, const py::handle& v) { g.origin = toVec3(v); })
      .def_readwrite("cell_size", &CoverageGrid::cellSize)
      .def_readwrite("cols", &CoverageGrid::cols)
      .def_readwrite("rows", &CoverageGrid::rows)
      .def_readwrite("height", &CoverageGrid::height)
      .def_readwrite("cell_heights", &CoverageGrid::cellHeights)
      .def("cell_count", &CoverageGrid::cellCount)
      .def(
          "cell_center",
          [](const CoverageGrid& g, int row, int col) {
            return vec3ToArray(g.cellCenter(row, col));
          },
          py::arg("row"), py::arg("col"));

  py::class_<CoverageResult>(m, "CoverageResult")
      .def(py::init<>())
      .def_readonly("grid", &CoverageResult::grid)
      .def_readonly("simulation_id", &CoverageResult::simulationId)
      .def_readonly("frequency_hz", &CoverageResult::frequencyHz)
      .def_property_readonly(
          "power_dbm",
          [](const CoverageResult& c) { return toArray1d(c.powerDbm); })
      .def_property_readonly(
          "path_loss_db",
          [](const CoverageResult& c) { return toArray1d(c.pathLossDb); })
      .def_property_readonly(
          "sinr_db",
          [](const CoverageResult& c) { return toArray1d(c.sinrDb); })
      .def("power_at", &CoverageResult::powerAt, py::arg("row"), py::arg("col"))
      .def_readonly_static("NoSignal", &CoverageResult::NoSignal);
}

void bindSimulator(py::module_& m) {
  py::class_<Simulator>(m, "Simulator")
      .def(py::init<SimulationSettings>(),
           py::arg("settings") = SimulationSettings{})
      .def("settings", &Simulator::settings,
           py::return_value_policy::reference_internal)
      // run/run_coverage are long pure-C++ computes touching no Python objects;
      // release the GIL so other Python threads (and concurrent sims) proceed.
      .def("run", &Simulator::run, py::arg("scene"),
           py::call_guard<py::gil_scoped_release>())
      .def("run_coverage", &Simulator::runCoverage, py::arg("scene"),
           py::arg("grid"), py::call_guard<py::gil_scoped_release>());
}

void bindExporters(py::module_& m) {
  py::module_ io = m.def_submodule("io", "Result exporters (JSON/CSV/GeoJSON/glTF).");

  // JSON
  io.def("result_to_json_string", &io::resultToJsonString, py::arg("result"));
  io.def("export_result_json", &io::exportResultJson, py::arg("result"),
         py::arg("path"));
  io.def("result_from_json_string", &io::resultFromJsonString, py::arg("text"));
  io.def("load_result_json", &io::loadResultJson, py::arg("path"));
  io.def("coverage_to_json_string", &io::coverageToJsonString,
         py::arg("coverage"));
  io.def("export_coverage_json", &io::exportCoverageJson, py::arg("coverage"),
         py::arg("path"));

  // CSV
  io.def("receivers_to_csv_string", &io::receiversToCsvString,
         py::arg("result"));
  io.def("export_receivers_csv", &io::exportReceiversCsv, py::arg("result"),
         py::arg("path"));
  io.def("coverage_to_csv_string", &io::coverageToCsvString,
         py::arg("coverage"));
  io.def("export_coverage_csv", &io::exportCoverageCsv, py::arg("coverage"),
         py::arg("path"));

  // GeoJSON
  io.def("receivers_to_geojson_string", &io::receiversToGeoJsonString,
         py::arg("result"));
  io.def("export_receivers_geojson", &io::exportReceiversGeoJson,
         py::arg("result"), py::arg("path"));
  io.def("paths_to_geojson_string", &io::pathsToGeoJsonString,
         py::arg("result"));
  io.def("export_paths_geojson", &io::exportPathsGeoJson, py::arg("result"),
         py::arg("path"));
  io.def("coverage_to_geojson_string", &io::coverageToGeoJsonString,
         py::arg("coverage"));
  io.def("export_coverage_geojson", &io::exportCoverageGeoJson,
         py::arg("coverage"), py::arg("path"));

  // glTF
  io.def("paths_to_gltf_string", &io::pathsToGltfString, py::arg("result"),
         py::arg("include_receivers") = true);
  io.def("export_paths_gltf", &io::exportPathsGltf, py::arg("result"),
         py::arg("path"), py::arg("include_receivers") = true);
}

}  // namespace

PYBIND11_MODULE(_native, m) {
  m.doc() = "RFTraceKit native extension (pybind11 bindings for the C++ core).";
  m.attr("__version__") = "0.1.0";

  bindEnums(m);
  bindMaterial(m);
  bindAntenna(m);
  bindGeometry(m);
  bindSceneTypes(m);
  bindScene(m);
  bindSettings(m);
  bindResults(m);
  bindCoverage(m);
  bindSimulator(m);
  bindExporters(m);
}
