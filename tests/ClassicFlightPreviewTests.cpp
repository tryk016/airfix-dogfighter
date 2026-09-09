#include "airfix/simulation/ClassicFlightPreview.hpp"
#include <cfenv>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace airfix::simulation;
void require(bool v, const char *text) {
  if (!v)
    throw std::runtime_error(text);
}
void near(double a, double b, double eps, const char *text) {
  require(std::abs(a - b) <= eps, text);
}
auto create(LegacyAircraftTypeId id = LegacyAircraftTypeId::mustang) {
  auto f = ClassicFlightPreview::create(id, {1, 2, 3});
  require(f.has_value(), "create");
  return *f;
}
void clockTests() {
  ClassicFlightClock a, b;
  unsigned count = 0;
  for (unsigned i = 0; i < 1000; ++i)
    count += a.consume(1'000'000, true).steps;
  require(count == 83, "12 ms clock, not 60Hz");
  unsigned other = 0;
  for (unsigned i = 0; i < 40; ++i)
    other += b.consume(25'000'000, true).steps;
  require(other == count, "partition invariant");
  require(a.consume(8'000'000, true).steps == 1, "remainder retained");
  auto stall = b.consume(240'000'000, true);
  require(stall.steps == 8 && stall.droppedSteps == 12, "bounded catchup");
  (void)b.consume(4'000'000, true);
  require(b.consume(std::numeric_limits<std::uint64_t>::max(), false).steps ==
              0,
          "pause");
  require(b.consume(11'999'999, true).steps == 0, "no resume debt");
  require(b.consume(1, true).steps == 1, "resume fractional");
  const auto huge = b.consume(std::numeric_limits<std::uint64_t>::max(), true);
  require(huge.steps == 8 && huge.droppedSteps > 1000, "overflow-safe clock");
}
void initialization() {
  for (const auto &record : legacyAircraftTypeCatalog()) {
    auto f = create(record.id);
    near(f.mass(), 0.875 * record.parameters.rigidBodyMassScale(), 1e-5,
         "mass contributions");
    require(f.state().position == FlightVector{1, 2, 3},
            "authored spawn not shifted");
    require(f.state().linearMomentum == FlightVector{} &&
                f.state().pendingForce == FlightVector{},
            "no invented launch speed");
    require(f.rotation() == flightIdentity, "identity");
    const double B = record.parameters.rigidBodyMassScale();
    const double ix =
        B *
        (0.375 * ((.055 * .055 + .05 * .05) + (.055 * .055 + .15 * .15)) / 12 +
         .125 * .1 * .1);
    near(f.bodyInverseInertia()[0], 1 / ix, std::abs(1 / ix) * 2e-6,
         "constructor inertia X");
  }
  require(
      !ClassicFlightPreview::create(static_cast<LegacyAircraftTypeId>(255), {}),
      "unknown type rejected");
  require(
      !ClassicFlightPreview::create(LegacyAircraftTypeId::mustang, {NAN, 0, 0}),
      "nonfinite spawn rejected");
  auto bad = flightIdentity;
  bad[0][0] = -1;
  require(!ClassicFlightPreview::create(LegacyAircraftTypeId::mustang, {}, bad),
          "reflection is not rotation");
  bad = flightIdentity;
  bad[0][1] = 0.1F;
  require(!ClassicFlightPreview::create(LegacyAircraftTypeId::mustang, {}, bad),
          "shear rejected");
  for (unsigned axis = 0; axis < 3; ++axis) {
    auto m = flightIdentity;
    m[(axis + 1) % 3][(axis + 1) % 3] = -1;
    m[(axis + 2) % 3][(axis + 2) % 3] = -1;
    auto f = ClassicFlightPreview::create(LegacyAircraftTypeId::mustang, {}, m);
    require(f.has_value() && f->rotation() == m,
            "180-degree quaternion conversion");
  }
  const FlightRotation quarterTurn{{{0, 0, -1}, {0, 1, 0}, {1, 0, 0}}};
  auto q = ClassicFlightPreview::create(LegacyAircraftTypeId::mustang, {},
                                        quarterTurn);
  require(q.has_value(), "quarter turn accepted");
  for (unsigned c = 0; c < 3; ++c)
    for (unsigned r = 0; r < 3; ++r)
      near(q->rotation()[c][r], quarterTurn[c][r], 2e-7,
           "quaternion direction");
}
void phaseOrderAndControls() {
  auto f =
      *ClassicFlightPreview::create(LegacyAircraftTypeId::mustang, {0, 2, 0});
  const auto p = f.state().position;
  require(f.tryStep({0, 0, 255}), "first step");
  require(f.state().position == p && f.state().linearMomentum == FlightVector{},
          "first step builds forces only");
  require(f.state().pendingForce[1] < 0 && f.state().pendingForce[2] > 0,
          "gravity and thrust");
  const float thrust = f.state().pendingForce[2];
  require(f.tryStep({}), "second step");
  require(f.state().position == p, "Euler position uses old momentum");
  require(f.state().linearMomentum[1] < 0 && f.state().linearMomentum[2] > 0,
          "old force integrated");
  near(f.state().linearMomentum[2],
       double(thrust) * classicFlightStepSeconds * .994, 1e-8,
       "next-step force and final damping");
  require(f.tryStep({}), "third step");
  require(f.state().position[1] < p[1] && f.state().position[2] > p[2],
          "real displacement");
  const auto unchanged = f.state();
  require(!f.tryStep({33, 0, 0}) && !f.tryStep({0, -33, 0}) &&
              !f.tryStep({0, 0, 256}),
          "payload bounds");
  require(f.state().completedSteps == unchanged.completedSteps &&
              f.state().position == unchanged.position &&
              f.state().pendingForce == unchanged.pendingForce &&
              f.state().thrust == unchanged.thrust,
          "atomic reject");
  auto neutral = create(), pitch = create(), bank = create();
  require(neutral.tryStep({}) && pitch.tryStep({32, 0, 0}) &&
              bank.tryStep({0, 32, 0}),
          "controls accepted");
  require(pitch.state().pendingTorque[0] > neutral.state().pendingTorque[0],
          "pitch torque direction");
  require(bank.state().pendingTorque[2] < neutral.state().pendingTorque[2],
          "bank torque direction");
  require(bank.tryStep({}) && bank.tryStep({}), "orientation integration");
  require(bank.state().quaternion[3] < 0,
          "bank changes rotation, not direct position");
}
void replayAndFiniteDomain() {
  for (const auto &record : legacyAircraftTypeCatalog()) {
    auto a = create(record.id), b = create(record.id);
    for (unsigned step = 0; step < 2500; ++step) {
      const ClassicFlightControls controls{step % 400 < 200 ? 8 : -8,
                                           step % 800 < 400 ? 12 : -12,
                                           step < 80 ? 255 : 0};
      require(a.tryStep(controls) && b.tryStep(controls),
              "long free-air trajectory remains finite");
      require(a.state().position == b.state().position &&
                  a.state().quaternion == b.state().quaternion &&
                  a.state().linearMomentum == b.state().linearMomentum,
              "repeatable fixed-step replay");
      double norm = 0;
      for (float v : a.state().quaternion)
        norm += double(v) * v;
      near(norm, 1, 3e-7, "unit quaternion");
    }
    require(a.state().completedSteps == 2500, "all replay steps");
  }
  auto a = create();
  const auto saved = a.state();
  const int original = std::fegetround();
  require(std::fesetround(FE_DOWNWARD) == 0, "test rounding setup");
  const bool rejected = !a.tryStep({});
  const bool rejectedCreation =
      !ClassicFlightPreview::create(LegacyAircraftTypeId::mustang, {});
  require(std::fesetround(original) == 0, "restore rounding");
  require(rejected && rejectedCreation &&
              a.state().position == saved.position &&
              a.state().completedSteps == 0,
          "foreign rounding environment rejected without mutation");
}

void yawCovariance() {
  // An independent geometrical invariant: rotating the whole initial world
  // around gravity must rotate the trajectory, not change its flight law.
  auto a =
      *ClassicFlightPreview::create(LegacyAircraftTypeId::mustang, {0, 2, 0});
  auto b = *ClassicFlightPreview::create(
      LegacyAircraftTypeId::mustang, {0, 2, 0},
      FlightRotation{{{0, 0, -1}, {0, 1, 0}, {1, 0, 0}}});
  for (unsigned step = 0; step < 300; ++step) {
    ClassicFlightControls controls{8, 4, step < 80 ? 255 : 0};
    require(a.tryStep(controls) && b.tryStep(controls), "yaw-covariant steps");
    near(b.state().position[0], a.state().position[2], 2e-5,
         "yaw rotates X displacement");
    near(b.state().position[1], a.state().position[1], 2e-5,
         "yaw preserves height");
    near(b.state().position[2], -a.state().position[0], 2e-5,
         "yaw rotates Z displacement");
    for (unsigned col = 0; col < 3; ++col) {
      near(b.rotation()[col][0], a.rotation()[col][2], 2e-5,
           "yaw rotates basis X");
      near(b.rotation()[col][1], a.rotation()[col][1], 2e-5,
           "yaw rotates basis Y");
      near(b.rotation()[col][2], -a.rotation()[col][0], 2e-5,
           "yaw rotates basis Z");
    }
  }
}
} // namespace
int main() {
  try {
    clockTests();
    initialization();
    phaseOrderAndControls();
    replayAndFiniteDomain();
    yawCovariance();
    std::cout << "Classic flight preview tests passed (port policy, not native "
                 "parity).\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
