#include "airfix/simulation/ClassicFlightPreview.hpp"
#include "airfix/simulation/LegacyAircraftPitchBankEventReducer.hpp"
#include "airfix/simulation/LegacyAircraftThrustEventReducer.hpp"

#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <limits>

namespace airfix::simulation {
namespace {
static_assert(std::numeric_limits<float>::is_iec559 && sizeof(float) == 4);
static_assert(std::numeric_limits<double>::is_iec559 && sizeof(double) == 8);
using V = FlightVector;
using M = FlightRotation;

float store(double v) noexcept {
  if (!std::isfinite(v) || std::abs(v) > std::numeric_limits<float>::max())
    return std::numeric_limits<float>::quiet_NaN();
  return static_cast<float>(v);
}
template <class T> bool finite(const T &values) noexcept {
  for (const auto v : values)
    if (!std::isfinite(v))
      return false;
  return true;
}
double dot(const V &a, const V &b) noexcept {
  return (double(a[0]) * b[0] + double(a[2]) * b[2]) + double(a[1]) * b[1];
}
V scale(const V &v, double s) noexcept {
  return {store(v[0] * s), store(v[1] * s), store(v[2] * s)};
}
V add(const V &a, const V &b) noexcept {
  return {store(double(a[0]) + b[0]), store(double(a[1]) + b[1]),
          store(double(a[2]) + b[2])};
}
V cross(const V &a, const V &b) noexcept {
  return {store(double(a[1]) * b[2] - double(a[2]) * b[1]),
          store(double(a[2]) * b[0] - double(a[0]) * b[2]),
          store(double(a[0]) * b[1] - double(a[1]) * b[0])};
}
V transform(const M &m, const V &v) noexcept {
  V out{};
  for (unsigned i = 0; i < 3; ++i)
    out[i] = store((double(m[0][i]) * v[0] + double(m[2][i]) * v[2]) +
                   double(m[1][i]) * v[1]);
  return out;
}
V inverseTransform(const M &m, const V &v) noexcept {
  return {store(dot(m[0], v)), store(dot(m[1], v)), store(dot(m[2], v))};
}
M rotationOf(const std::array<float, 4> &q) noexcept {
  const double w = q[0], x = q[1], y = q[2], z = q[3];
  return {{{store(1 - 2 * (y * y + z * z)), store(2 * (w * z + x * y)),
            store(2 * (x * z - w * y))},
           {store(2 * (x * y - w * z)), store(1 - 2 * (x * x + z * z)),
            store(2 * (w * x + y * z))},
           {store(2 * (w * y + x * z)), store(2 * (y * z - w * x)),
            store(1 - 2 * (x * x + y * y))}}};
}
bool normalize(std::array<float, 4> &q) noexcept {
  double norm = 0;
  for (const float v : q)
    norm += double(v) * v;
  if (!std::isfinite(norm) || norm <= 0)
    return false;
  const double length = std::sqrt(norm);
  for (float &v : q)
    v = store(v / length);
  return finite(q);
}
bool quaternionOf(const M &m, std::array<float, 4> &q) noexcept {
  for (unsigned i = 0; i < 3; ++i) {
    if (!finite(m[i]) || std::abs(dot(m[i], m[i]) - 1) > 0.0001)
      return false;
    for (unsigned j = 0; j < i; ++j)
      if (std::abs(dot(m[i], m[j])) > 0.0001)
        return false;
  }
  if (std::abs(dot(cross(m[0], m[1]), m[2]) - 1) > 0.0001)
    return false;
  const double tr = double(m[0][0]) + m[1][1] + m[2][2];
  if (tr > 0) {
    const double s = 2 * std::sqrt(tr + 1);
    q = {store(s / 4), store((double(m[1][2]) - m[2][1]) / s),
         store((double(m[2][0]) - m[0][2]) / s),
         store((double(m[0][1]) - m[1][0]) / s)};
  } else {
    unsigned i = 0;
    if (m[1][1] > m[i][i])
      i = 1;
    if (m[2][2] > m[i][i])
      i = 2;
    const unsigned j = (i + 1) % 3, k = (i + 2) % 3;
    const double s = 2 * std::sqrt(1 + double(m[i][i]) - m[j][j] - m[k][k]);
    q[0] = store((double(m[j][k]) - m[k][j]) / s);
    q[i + 1] = store(s / 4);
    q[j + 1] = store((double(m[j][i]) + m[i][j]) / s);
    q[k + 1] = store((double(m[k][i]) + m[i][k]) / s);
  }
  return normalize(q);
}
bool valid(const ClassicFlightState &s) noexcept {
  return finite(s.position) && finite(s.quaternion) &&
         finite(s.linearMomentum) && finite(s.angularMomentum) &&
         finite(s.pendingForce) && finite(s.pendingTorque) &&
         std::isfinite(s.thrust.targetThrust) &&
         std::isfinite(s.thrust.thrustApply) &&
         std::isfinite(s.thrust.smoothedThrust) &&
         std::isfinite(s.smoothedOrientationM01) &&
         std::isfinite(s.engine.engineStartElapsedSeconds);
}
void force(ClassicFlightState &s, const V &f, const V &offset) noexcept {
  s.pendingForce = add(s.pendingForce, f);
  s.pendingTorque = add(s.pendingTorque, cross(offset, f));
}
} // namespace

std::optional<ClassicFlightPreview>
ClassicFlightPreview::create(LegacyAircraftTypeId type,
                             const FlightVector &position,
                             const FlightRotation &rotation) noexcept {
  if (std::fegetround() != FE_TONEAREST || !finite(position))
    return std::nullopt;
  const auto *record = findLegacyAircraftType(type);
  if (!record)
    return std::nullopt;
  ClassicFlightPreview out;
  if (!quaternionOf(rotation, out.state_.quaternion))
    return std::nullopt;
  out.state_.position = position;
  const auto &p = record->parameters;
  out.massScale_ = p.rigidBodyMassScale();
  out.thrustFactor_ = store(double(p.thrustPercent()) * double(0.01F));
  out.dampingFactor_ = store(double(p.forceDampingPercent()) * double(0.01F));
  out.health_ = p.initialHealth();
  // AirCraft constructor: two blocks, then a particle at (0,0,0.1).
  // Do not replace the verified 0.875*B total by the nominal B field.
  const float blockMass = store(0.375 * double(out.massScale_));
  const float particleMass = store(0.125 * double(out.massScale_));
  out.mass_ = double(blockMass) + blockMass + particleMass;
  V inertia{};
  constexpr std::array<V, 2> dimensions{
      {{0.2F, 0.055F, 0.05F}, {0.05F, 0.055F, 0.15F}}};
  const double twelfth = std::bit_cast<float>(0x3DAAAAABU);
  for (unsigned b = 0; b < 2; ++b)
    for (unsigned i = 0; i < 3; ++i) {
      const double a = dimensions[b][(i + 1) % 3],
                   c = dimensions[b][(i + 2) % 3];
      inertia[i] =
          store(double(inertia[i]) + (a * a + c * c) * blockMass * twelfth);
    }
  for (unsigned i = 0; i < 2; ++i)
    inertia[i] = store(double(inertia[i]) + double(0.1F) * 0.1F * particleMass);
  for (unsigned i = 0; i < 3; ++i)
    out.inverseInertia_[i] = store(1 / double(inertia[i]));
  if (!(out.mass_ > 0) || !finite(out.inverseInertia_))
    return std::nullopt;
  return out;
}

FlightRotation ClassicFlightPreview::rotation() const noexcept {
  return rotationOf(state_.quaternion);
}

bool ClassicFlightPreview::tryStep(
    const ClassicFlightControls controls) noexcept {
  if (std::fegetround() != FE_TONEAREST || controls.pitchPayload < -32 ||
      controls.pitchPayload > 32 || controls.bankPayload < -32 ||
      controls.bankPayload > 32 || controls.thrustApplyPayload < -255 ||
      controls.thrustApplyPayload > 255 ||
      state_.completedSteps == std::numeric_limits<std::uint64_t>::max())
    return false;
  auto s = state_;
  const auto pitch = legacyAircraftDecodeNativePitchBankEvent(
      {LegacyAircraftNativePitchBankEvent::pitchSet, controls.pitchPayload});
  const auto bank = legacyAircraftDecodeNativePitchBankEvent(
      {LegacyAircraftNativePitchBankEvent::bankSet, controls.bankPayload});
  const auto thrust = legacyAircraftDecodeNativeThrustEvent(
      {LegacyAircraftNativeThrustEvent::thrustApply,
       controls.thrustApplyPayload});
  if (!pitch.decoded() || !bank.decoded() || !thrust.decoded())
    return false;
  const double dt = classicFlightStepSeconds;
  const M oldR = rotationOf(s.quaternion);
  // R * IbodyInv * transpose(R), then native row-vector L * W.
  M w{};
  for (unsigned col = 0; col < 3; ++col)
    for (unsigned row = 0; row < 3; ++row) {
      double value = 0;
      for (unsigned k : {0U, 2U, 1U})
        value += double(store(double(oldR[k][row]) * inverseInertia_[k])) *
                 oldR[k][col];
      w[col][row] = store(value);
    }
  const V omega = inverseTransform(w, s.angularMomentum);
  const auto q = s.quaternion;
  const V qv{q[1], q[2], q[3]};
  const V qdv = scale(add(cross(omega, qv), scale(omega, q[0])), 0.5);
  const std::array<float, 4> qd{store(-0.5 * dot(omega, qv)), qdv[0], qdv[1],
                                qdv[2]};
  for (unsigned i = 0; i < 3; ++i) {
    s.position[i] =
        store(double(s.position[i]) + dt * store(s.linearMomentum[i] / mass_));
    s.linearMomentum[i] =
        store(double(s.linearMomentum[i]) + dt * s.pendingForce[i]);
    const float derivative =
        store(double(s.pendingTorque[i]) - 6.0 * double(s.angularMomentum[i]));
    s.angularMomentum[i] =
        store(double(s.angularMomentum[i]) + dt * derivative);
  }
  for (unsigned i = 0; i < 4; ++i)
    s.quaternion[i] = store(double(q[i]) + dt * qd[i]);
  if (!normalize(s.quaternion))
    return false;
  s.pendingForce = {};
  s.pendingTorque = {};
  const M r = rotationOf(s.quaternion);
  const V velocity = scale(s.linearMomentum, 1 / mass_);
  const V localVelocity = inverseTransform(r, velocity);
  const double vz = std::max(double(localVelocity[2]), 0.0);
  const double b = massScale_;
  const float d = store(std::max((2 - vz) * double(0.01F), 0.0));
  const float c = store((0.5 * vz + 1) * dampingFactor_);
  s.thrust.thrustApply = thrust.write->value;
  const auto smoothed = legacyAircraftAdvanceThrustControl(
      s.thrust, {health_, s.engine.engineStartTransitionActive});
  if (!smoothed)
    return false;
  s.thrust = *smoothed;
  force(s, {0, store(-double(0.54555553F) * b), 0},
        transform(r, {0, store(0.1 * d), d}));
  const float lift = store(std::min(0.7 * vz * b, 0.2 * b));
  if (lift > 0.05 * b)
    force(s, scale(r[1], lift), scale(r[2], d));
  force(s, scale(r[2], 2 * double(s.thrust.smoothedThrust) * thrustFactor_ * b),
        {});
  const auto torque = [&](const V &offset, const V &f) {
    s.pendingTorque = add(s.pendingTorque, cross(offset, f));
  };
  torque(scale(r[2], -1),
         scale(r[1], double(pitch.write->value()) * b * c * 0.008));
  torque(scale(r[0], -1),
         scale(r[1], double(bank.write->value()) * b * c * 0.014));
  const double m01 = r[0][1];
  s.smoothedOrientationM01 = store(double(s.smoothedOrientationM01) +
                                   0.05 * (m01 - s.smoothedOrientationM01));
  torque(scale(r[0], -1), scale(r[1], m01 * b * c * 0.024));
  torque(r[2], scale(r[1], std::abs(m01 * b * c * 0.014)));
  force(s, scale(r[0], m01 * b * c * 0.03), scale(r[2], -0.75));
  V localMomentum = inverseTransform(r, s.linearMomentum);
  const double dampXY = 0.93 + 0.01 * double(dampingFactor_);
  localMomentum[0] = store(localMomentum[0] * dampXY);
  localMomentum[1] = store(localMomentum[1] * dampXY);
  localMomentum[2] = store(double(localMomentum[2]) * 0.994);
  s.linearMomentum = transform(r, localMomentum);
  s.angularMomentum = scale(s.angularMomentum, 0.91);
  const auto engine = legacyAircraftAdvanceEngineAudio(
      s.engine,
      {classicFlightStepSeconds, s.thrust.smoothedThrust,
       store(std::sqrt(dot(velocity, velocity))), s.smoothedOrientationM01});
  if (!engine)
    return false;
  s.engine = engine->state;
  s.thrust.smoothedThrust = engine->smoothedThrust;
  ++s.completedSteps;
  if (!valid(s))
    return false;
  state_ = s;
  return true;
}

ClassicFlightClockBatch ClassicFlightClock::consume(std::uint64_t elapsed,
                                                    bool running) noexcept {
  if (!running) {
    reset();
    return {};
  }
  // Quotient/remainder split avoids uint64 overflow even for hostile elapsed.
  auto count = elapsed / classicFlightStepNanoseconds;
  remainder_ += elapsed % classicFlightStepNanoseconds;
  count += remainder_ / classicFlightStepNanoseconds;
  remainder_ %= classicFlightStepNanoseconds;
  const auto steps =
      static_cast<std::uint32_t>(std::min(count, std::uint64_t{8}));
  return {steps, count - steps};
}
} // namespace airfix::simulation
