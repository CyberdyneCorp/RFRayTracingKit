#include <gtest/gtest.h>

#include "rftrace/scene.hpp"

using namespace rftrace;

TEST(Scene, AssembleAndResolveMaterial) {
  Scene scene;
  Material concrete = materials::preset("concrete");
  const int idx = scene.addMaterial(concrete);
  scene.addMesh({Triangle{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}}, "concrete");

  ASSERT_EQ(scene.triangles().size(), 1u);
  EXPECT_EQ(scene.triangleMaterialIndex(0), idx);
  EXPECT_EQ(scene.materialForTriangle(0).name, "concrete");
}

TEST(Scene, UnassignedMeshUsesDefaultMaterial) {
  Scene scene;
  scene.addMesh({Triangle{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}}, "");
  EXPECT_EQ(scene.materialForTriangle(0).name, "default");
}

TEST(Scene, UnknownMaterialNameThrows) {
  Scene scene;
  EXPECT_THROW(scene.addMesh({Triangle{}}, "nonexistent"), SceneError);
}

TEST(Scene, DuplicateTransmitterIdRejected) {
  Scene scene;
  Transmitter tx;
  tx.id = "tx1";
  scene.addTransmitter(tx);
  EXPECT_THROW(scene.addTransmitter(tx), SceneError);
}

TEST(Scene, DuplicateReceiverIdRejected) {
  Scene scene;
  Receiver rx;
  rx.id = "rx1";
  scene.addReceiver(rx);
  EXPECT_THROW(scene.addReceiver(rx), SceneError);
}

TEST(Scene, CoordinateSystemIsZUpByDefault) {
  Scene scene;
  EXPECT_EQ(scene.coordinateSystem().up, UpAxis::Z);
  EXPECT_EQ(scene.coordinateSystem().units, "meters");
}

TEST(Antenna, OmnidirectionalConstantGain) {
  AntennaPattern omni = AntennaPattern::Omnidirectional(2.5);
  EXPECT_NEAR(omni.gainTowards({1, 0, 0}), 2.5, 1e-12);
  EXPECT_NEAR(omni.gainTowards({0, -1, 0.3}), 2.5, 1e-12);
}

TEST(Antenna, DirectionalGainInterpolates) {
  AntennaPattern pat;
  pat.omni = false;
  pat.peakGainDbi = 10.0;
  pat.boresight = {1, 0, 0};
  pat.up = {0, 0, 1};
  pat.azimuthCutDb = {{0.0, 0.0}, {90.0, -10.0}, {180.0, -20.0}};

  EXPECT_NEAR(pat.gainTowards({1, 0, 0}), 10.0, 1e-9);   // boresight
  EXPECT_NEAR(pat.gainTowards({0, 1, 0}), 0.0, 1e-6);    // 90° -> 10-10
  EXPECT_NEAR(pat.gainTowards({1, 1, 0}), 5.0, 1e-6);    // 45° -> 10-5
}
