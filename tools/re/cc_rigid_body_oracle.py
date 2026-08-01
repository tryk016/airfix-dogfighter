#!/usr/bin/env python3
"""Exact finite-domain oracle for the recovered CcRigidBody x87 schedule.

This module is research tooling, not a game-runtime implementation.  Every
arithmetic result is rounded through an explicitly selected x87 precision and
rounding policy, and every recovered binary32 spill is represented explicitly.
Unsupported non-finite/native-exception cases fail closed instead of claiming
an unobserved game-process result.
"""

from __future__ import annotations

from dataclasses import dataclass
from fractions import Fraction
from math import isqrt
from typing import Final


PRECISION_BITS: Final = {"pc24": 24, "pc53": 53, "pc64": 64}
ROUNDING_MODES: Final = frozenset({"nearest", "down", "up", "zero"})


class OracleInputError(ValueError):
    """A stable fail-closed rejection from the finite oracle domain."""

    def __init__(self, code: str) -> None:
        super().__init__(code)
        self.code = code


@dataclass(frozen=True)
class X87Policy:
    precision: str
    rounding: str

    def __post_init__(self) -> None:
        if self.precision not in PRECISION_BITS:
            raise OracleInputError("unsupported-precision")
        if self.rounding not in ROUNDING_MODES:
            raise OracleInputError("unsupported-rounding")

    @property
    def precision_bits(self) -> int:
        return PRECISION_BITS[self.precision]


@dataclass(frozen=True)
class FiniteValue:
    fraction: Fraction
    negative_zero: bool = False

    def __post_init__(self) -> None:
        if self.fraction != 0 and self.negative_zero:
            raise ValueError("only zero can carry an explicit zero sign")

    @property
    def negative(self) -> bool:
        return self.fraction < 0 or (
            self.fraction == 0 and self.negative_zero
        )


POSITIVE_ZERO: Final = FiniteValue(Fraction(0), False)
NEGATIVE_ZERO: Final = FiniteValue(Fraction(0), True)
ONE: Final = FiniteValue(Fraction(1), False)
HALF: Final = FiniteValue(Fraction(1, 2), False)


def _finite_zero(negative: bool) -> FiniteValue:
    return NEGATIVE_ZERO if negative else POSITIVE_ZERO


def _scaled_fraction(significand: int, shift: int) -> Fraction:
    result = Fraction(significand, 1)
    if shift >= 0:
        return result * (1 << shift)
    return result / (1 << -shift)


def load_binary32(bits: int) -> FiniteValue:
    if isinstance(bits, bool) or not 0 <= bits < 1 << 32:
        raise OracleInputError("invalid-binary32-bits")
    negative = bool(bits >> 31)
    exponent = (bits >> 23) & 0xFF
    fraction = bits & 0x7FFFFF
    if exponent == 0xFF:
        raise OracleInputError("nonfinite-binary32")
    if exponent == 0:
        if fraction == 0:
            return _finite_zero(negative)
        magnitude = _scaled_fraction(fraction, -149)
    else:
        magnitude = _scaled_fraction((1 << 23) | fraction, exponent - 150)
    return FiniteValue(-magnitude if negative else magnitude)


def load_binary64(bits: int) -> FiniteValue:
    if isinstance(bits, bool) or not 0 <= bits < 1 << 64:
        raise OracleInputError("invalid-binary64-bits")
    negative = bool(bits >> 63)
    exponent = (bits >> 52) & 0x7FF
    fraction = bits & ((1 << 52) - 1)
    if exponent == 0x7FF:
        raise OracleInputError("nonfinite-binary64")
    if exponent == 0:
        if fraction == 0:
            return _finite_zero(negative)
        magnitude = _scaled_fraction(fraction, -1074)
    else:
        magnitude = _scaled_fraction(
            (1 << 52) | fraction, exponent - 1023 - 52
        )
    return FiniteValue(-magnitude if negative else magnitude)


def _floor_log2(value: Fraction) -> int:
    if value <= 0:
        raise ValueError("floor_log2 requires a positive value")
    numerator = value.numerator
    denominator = value.denominator
    exponent = numerator.bit_length() - denominator.bit_length()
    if exponent >= 0:
        if numerator < denominator << exponent:
            exponent -= 1
    elif numerator << -exponent < denominator:
        exponent -= 1
    return exponent


def _round_integer(
    numerator: int, denominator: int, negative: bool, rounding: str
) -> int:
    quotient, remainder = divmod(numerator, denominator)
    if remainder == 0:
        return quotient
    increment = False
    if rounding == "nearest":
        comparison = remainder * 2 - denominator
        increment = comparison > 0 or (comparison == 0 and quotient & 1 == 1)
    elif rounding == "down":
        increment = negative
    elif rounding == "up":
        increment = not negative
    elif rounding != "zero":
        raise OracleInputError("unsupported-rounding")
    return quotient + int(increment)


def _round_fraction_to_precision(
    value: FiniteValue, precision: int, rounding: str
) -> FiniteValue:
    if value.fraction == 0:
        return value
    negative = value.fraction < 0
    magnitude = abs(value.fraction)
    exponent = _floor_log2(magnitude)
    shift = exponent - (precision - 1)
    if shift >= 0:
        numerator = magnitude.numerator
        denominator = magnitude.denominator << shift
    else:
        numerator = magnitude.numerator << -shift
        denominator = magnitude.denominator
    significand = _round_integer(numerator, denominator, negative, rounding)
    if significand == 1 << precision:
        significand >>= 1
        exponent += 1
    rounded = _scaled_fraction(significand, exponent - (precision - 1))
    return FiniteValue(-rounded if negative else rounded)


def _pc(value: FiniteValue, policy: X87Policy) -> FiniteValue:
    return _round_fraction_to_precision(
        value, policy.precision_bits, policy.rounding
    )


def negate(value: FiniteValue) -> FiniteValue:
    if value.fraction == 0:
        return _finite_zero(not value.negative_zero)
    return FiniteValue(-value.fraction)


def multiply(
    left: FiniteValue, right: FiniteValue, policy: X87Policy
) -> FiniteValue:
    if left.fraction == 0 or right.fraction == 0:
        return _finite_zero(left.negative != right.negative)
    return _pc(FiniteValue(left.fraction * right.fraction), policy)


def _exact_zero_sum_sign(
    left: FiniteValue, right: FiniteValue, rounding: str
) -> bool:
    if left.fraction == 0 and right.fraction == 0:
        if left.negative == right.negative:
            return left.negative
    return rounding == "down"


def add(
    left: FiniteValue, right: FiniteValue, policy: X87Policy
) -> FiniteValue:
    exact = left.fraction + right.fraction
    if exact == 0:
        return _finite_zero(
            _exact_zero_sum_sign(left, right, policy.rounding)
        )
    return _pc(FiniteValue(exact), policy)


def subtract(
    left: FiniteValue, right: FiniteValue, policy: X87Policy
) -> FiniteValue:
    return add(left, negate(right), policy)


def divide(
    numerator: FiniteValue, denominator: FiniteValue, policy: X87Policy
) -> FiniteValue:
    if denominator.fraction == 0:
        raise OracleInputError("division-by-zero")
    if numerator.fraction == 0:
        return _finite_zero(numerator.negative != denominator.negative)
    return _pc(FiniteValue(numerator.fraction / denominator.fraction), policy)


def _round_sqrt_to_precision(
    value: FiniteValue, precision: int, rounding: str
) -> FiniteValue:
    if value.fraction < 0:
        raise OracleInputError("sqrt-negative")
    if value.fraction == 0:
        return value

    exponent = _floor_log2(value.fraction) // 2
    shift = 2 * (precision - 1 - exponent)
    if shift >= 0:
        numerator = value.fraction.numerator << shift
        denominator = value.fraction.denominator
    else:
        numerator = value.fraction.numerator
        denominator = value.fraction.denominator << -shift

    lower = isqrt(numerator // denominator)
    while (lower + 1) * (lower + 1) * denominator <= numerator:
        lower += 1
    while lower * lower * denominator > numerator:
        lower -= 1
    exact = lower * lower * denominator == numerator
    increment = False
    if not exact:
        if rounding == "up":
            increment = True
        elif rounding == "nearest":
            midpoint_numerator = (2 * lower + 1) ** 2 * denominator
            midpoint_denominator = 4
            comparison = (
                numerator * midpoint_denominator - midpoint_numerator
            )
            increment = comparison > 0 or (
                comparison == 0 and lower & 1 == 1
            )
        elif rounding not in {"down", "zero"}:
            raise OracleInputError("unsupported-rounding")
    significand = lower + int(increment)
    if significand == 1 << precision:
        significand >>= 1
        exponent += 1
    rounded = _scaled_fraction(significand, exponent - (precision - 1))
    return FiniteValue(rounded)


def square_root(value: FiniteValue, policy: X87Policy) -> FiniteValue:
    return _round_sqrt_to_precision(
        value, policy.precision_bits, policy.rounding
    )


def store_binary32(value: FiniteValue, rounding: str) -> int:
    if rounding not in ROUNDING_MODES:
        raise OracleInputError("unsupported-rounding")
    negative = value.negative
    sign_bit = 0x80000000 if negative else 0
    if value.fraction == 0:
        return sign_bit
    magnitude = abs(value.fraction)
    exponent = _floor_log2(magnitude)

    if exponent < -126:
        scaled = magnitude * (1 << 149)
        significand = _round_integer(
            scaled.numerator, scaled.denominator, negative, rounding
        )
        if significand == 0:
            return sign_bit
        if significand > 1 << 23:
            raise OracleInputError("binary32-subnormal-overflow")
        if significand == 1 << 23:
            return sign_bit | (1 << 23)
        return sign_bit | significand

    rounded = _round_fraction_to_precision(
        FiniteValue(-magnitude if negative else magnitude), 24, rounding
    )
    rounded_magnitude = abs(rounded.fraction)
    rounded_exponent = _floor_log2(rounded_magnitude)
    if rounded_exponent > 127:
        raise OracleInputError("binary32-overflow")
    if rounded_exponent < -126:
        return store_binary32(rounded, rounding)
    shift = rounded_exponent - 23
    if shift >= 0:
        significand = rounded_magnitude.numerator // (
            rounded_magnitude.denominator << shift
        )
    else:
        significand = (
            rounded_magnitude.numerator << -shift
        ) // rounded_magnitude.denominator
    if not (1 << 23) <= significand < 1 << 24:
        raise OracleInputError("binary32-encoding")
    return (
        sign_bit
        | ((rounded_exponent + 127) << 23)
        | (significand - (1 << 23))
    )


def _spill(value: FiniteValue, policy: X87Policy) -> int:
    return store_binary32(value, policy.rounding)


def _load_words(words: tuple[int, ...]) -> tuple[FiniteValue, ...]:
    return tuple(load_binary32(word) for word in words)


def _double(value: FiniteValue, policy: X87Policy) -> FiniteValue:
    return add(value, value, policy)


IDENTITY_MATRIX: Final = (
    0x3F800000,
    0,
    0,
    0,
    0x3F800000,
    0,
    0,
    0,
    0x3F800000,
)
ZERO3: Final = (0, 0, 0)


def _matrix_get(words: tuple[int, ...], row: int, column: int) -> FiniteValue:
    return load_binary32(words[row + 3 * column])


def transform_quaternion_to_matrix(
    quaternion_words: tuple[int, int, int, int], policy: X87Policy
) -> tuple[int, ...]:
    if len(quaternion_words) != 4:
        raise OracleInputError("quaternion-size")
    w, x, y, z = _load_words(quaternion_words)
    zz = multiply(z, z, policy)
    yy_word = _spill(multiply(y, y, policy), policy)
    xx_word = _spill(multiply(x, x, policy), policy)
    xy = multiply(x, y, policy)
    wz = multiply(w, z, policy)
    xz_word = _spill(multiply(x, z, policy), policy)
    wy_word = _spill(multiply(w, y, policy), policy)
    yz = multiply(y, z, policy)
    wx = multiply(w, x, policy)

    xx = load_binary32(xx_word)
    yy = load_binary32(yy_word)
    xz = load_binary32(xz_word)
    wy = load_binary32(wy_word)

    r00 = _spill(subtract(ONE, _double(add(yy, zz, policy), policy), policy), policy)
    r10 = _spill(_double(add(wz, xy, policy), policy), policy)
    r20 = _spill(_double(subtract(xz, wy, policy), policy), policy)
    r01 = _spill(_double(subtract(xy, wz, policy), policy), policy)
    r11 = _spill(subtract(ONE, _double(add(xx, zz, policy), policy), policy), policy)
    r21 = _spill(_double(add(wx, yz, policy), policy), policy)
    r02 = _spill(_double(add(wy, xz, policy), policy), policy)
    r12 = _spill(_double(subtract(yz, wx, policy), policy), policy)
    r22 = _spill(subtract(ONE, _double(add(xx, yy, policy), policy), policy), policy)
    return (r00, r10, r20, r01, r11, r21, r02, r12, r22)


def transpose_matrix(words: tuple[int, ...]) -> tuple[int, ...]:
    if len(words) != 9:
        raise OracleInputError("matrix-size")
    return tuple(words[column + 3 * row] for column in range(3) for row in range(3))


def multiply_matrices(
    left: tuple[int, ...], right: tuple[int, ...], policy: X87Policy
) -> tuple[int, ...]:
    if len(left) != 9 or len(right) != 9:
        raise OracleInputError("matrix-size")
    result = [0] * 9
    for row in range(3):
        for column in range(3):
            first = multiply(
                _matrix_get(right, 0, column),
                _matrix_get(left, row, 0),
                policy,
            )
            second = multiply(
                _matrix_get(right, 2, column),
                _matrix_get(left, row, 2),
                policy,
            )
            retained = add(first, second, policy)
            third = multiply(
                _matrix_get(left, row, 1),
                _matrix_get(right, 1, column),
                policy,
            )
            result[row + 3 * column] = _spill(add(retained, third, policy), policy)
    return tuple(result)


def multiply_row_vector(
    vector_words: tuple[int, int, int],
    matrix_words: tuple[int, ...],
    policy: X87Policy,
) -> tuple[int, int, int]:
    if len(vector_words) != 3:
        raise OracleInputError("vector-size")
    if len(matrix_words) != 9:
        raise OracleInputError("matrix-size")
    x, y, z = _load_words(vector_words)

    x_value = add(
        add(
            multiply(_matrix_get(matrix_words, 1, 0), y, policy),
            multiply(_matrix_get(matrix_words, 2, 0), z, policy),
            policy,
        ),
        multiply(_matrix_get(matrix_words, 0, 0), x, policy),
        policy,
    )
    y_value = add(
        add(
            multiply(_matrix_get(matrix_words, 1, 1), y, policy),
            multiply(_matrix_get(matrix_words, 0, 1), x, policy),
            policy,
        ),
        multiply(_matrix_get(matrix_words, 2, 1), z, policy),
        policy,
    )
    z_value = add(
        add(
            multiply(_matrix_get(matrix_words, 1, 2), y, policy),
            multiply(_matrix_get(matrix_words, 0, 2), x, policy),
            policy,
        ),
        multiply(_matrix_get(matrix_words, 2, 2), z, policy),
        policy,
    )
    return (
        _spill(x_value, policy),
        _spill(y_value, policy),
        _spill(z_value, policy),
    )


@dataclass(frozen=True)
class RigidBodyInput:
    mass_bits: int
    body_inverse_inertia_words: tuple[int, ...]
    damping_bits: int
    state_words: tuple[int, ...]
    force_words: tuple[int, ...]
    torque_words: tuple[int, ...]
    dt_bits: int

    def __post_init__(self) -> None:
        if len(self.body_inverse_inertia_words) != 9:
            raise OracleInputError("body-inverse-inertia-size")
        if len(self.state_words) != 13:
            raise OracleInputError("state-size")
        if len(self.force_words) != 3:
            raise OracleInputError("force-size")
        if len(self.torque_words) != 3:
            raise OracleInputError("torque-size")


@dataclass(frozen=True)
class AuxiliaryResult:
    rotation_words: tuple[int, ...]
    world_inverse_inertia_words: tuple[int, ...]
    linear_velocity_words: tuple[int, int, int]
    angular_velocity_words: tuple[int, int, int]


@dataclass(frozen=True)
class DeriveResult:
    auxiliary: AuxiliaryResult
    derivative_words: tuple[int, ...]
    stored_damping_xy_words: tuple[int, int]


@dataclass(frozen=True)
class StepResult:
    pre_step: DeriveResult
    unnormalized_state_words: tuple[int, ...]
    post_state_words: tuple[int, ...]
    post_step_auxiliary: AuxiliaryResult
    cleared_accumulator_words: tuple[int, int, int, int, int, int]


def _validate_input(input_value: RigidBodyInput) -> None:
    mass = load_binary64(input_value.mass_bits)
    if mass.fraction <= 0:
        raise OracleInputError("mass-not-positive")
    for word in (
        input_value.body_inverse_inertia_words
        + (input_value.damping_bits,)
        + input_value.state_words
        + input_value.force_words
        + input_value.torque_words
        + (input_value.dt_bits,)
    ):
        load_binary32(word)
    if load_binary32(input_value.dt_bits).fraction < 0:
        raise OracleInputError("dt-negative")


def calculate_auxiliary(
    input_value: RigidBodyInput,
    state_words: tuple[int, ...],
    policy: X87Policy,
) -> AuxiliaryResult:
    if len(state_words) != 13:
        raise OracleInputError("state-size")
    mass = load_binary64(input_value.mass_bits)
    if mass.fraction <= 0:
        raise OracleInputError("mass-not-positive")
    for word in state_words:
        load_binary32(word)

    quaternion = tuple(state_words[3:7])
    rotation = transform_quaternion_to_matrix(quaternion, policy)
    inverse_mass = divide(ONE, mass, policy)
    linear_momentum = _load_words(tuple(state_words[7:10]))
    linear_velocity = tuple(
        _spill(multiply(inverse_mass, component, policy), policy)
        for component in linear_momentum
    )
    first_product = multiply_matrices(
        rotation, input_value.body_inverse_inertia_words, policy
    )
    world_inverse_inertia = multiply_matrices(
        first_product, transpose_matrix(rotation), policy
    )
    angular_velocity = multiply_row_vector(
        tuple(state_words[10:13]), world_inverse_inertia, policy
    )
    return AuxiliaryResult(
        rotation_words=rotation,
        world_inverse_inertia_words=world_inverse_inertia,
        linear_velocity_words=linear_velocity,  # type: ignore[arg-type]
        angular_velocity_words=angular_velocity,
    )


def derive(input_value: RigidBodyInput, policy: X87Policy) -> DeriveResult:
    _validate_input(input_value)
    state = input_value.state_words
    auxiliary = calculate_auxiliary(input_value, state, policy)
    quaternion = _load_words(tuple(state[3:7]))
    omega = _load_words(auxiliary.angular_velocity_words)
    w, x, y, z = quaternion
    omega_x, omega_y, omega_z = omega

    scalar = negate(
        add(
            add(
                multiply(y, omega_y, policy),
                multiply(z, omega_z, policy),
                policy,
            ),
            multiply(omega_x, x, policy),
            policy,
        )
    )
    scalar_word = _spill(scalar, policy)

    cross_words = (
        _spill(
            subtract(
                multiply(z, omega_y, policy),
                multiply(y, omega_z, policy),
                policy,
            ),
            policy,
        ),
        _spill(
            subtract(
                multiply(x, omega_z, policy),
                multiply(z, omega_x, policy),
                policy,
            ),
            policy,
        ),
        _spill(
            subtract(
                multiply(y, omega_x, policy),
                multiply(omega_y, x, policy),
                policy,
            ),
            policy,
        ),
    )
    scalar_velocity_words = tuple(
        _spill(multiply(w, component, policy), policy) for component in omega
    )
    vector_words = tuple(
        _spill(
            add(load_binary32(cross), load_binary32(scaled), policy), policy
        )
        for cross, scaled in zip(cross_words, scalar_velocity_words)
    )
    quaternion_terms = (scalar_word,) + vector_words
    quaternion_derivative = tuple(
        _spill(multiply(HALF, load_binary32(word), policy), policy)
        for word in quaternion_terms
    )

    damping = load_binary32(input_value.damping_bits)
    angular_momentum = _load_words(tuple(state[10:13]))
    torque = _load_words(tuple(input_value.torque_words))
    damping_x_word = _spill(
        multiply(damping, angular_momentum[0], policy), policy
    )
    damping_y_word = _spill(
        multiply(damping, angular_momentum[1], policy), policy
    )
    damping_z = multiply(damping, angular_momentum[2], policy)
    angular_derivative = (
        _spill(
            subtract(torque[0], load_binary32(damping_x_word), policy), policy
        ),
        _spill(
            subtract(torque[1], load_binary32(damping_y_word), policy), policy
        ),
        _spill(subtract(torque[2], damping_z, policy), policy),
    )
    derivative = (
        auxiliary.linear_velocity_words
        + quaternion_derivative
        + tuple(input_value.force_words)
        + angular_derivative
    )
    return DeriveResult(
        auxiliary=auxiliary,
        derivative_words=derivative,
        stored_damping_xy_words=(damping_x_word, damping_y_word),
    )


def euler_lane(
    state_bits: int,
    dt_bits: int,
    derivative_bits: int,
    policy: X87Policy,
) -> int:
    state = load_binary32(state_bits)
    dt = load_binary32(dt_bits)
    derivative = load_binary32(derivative_bits)
    product = multiply(dt, derivative, policy)
    return _spill(add(product, state, policy), policy)


def normalize_quaternion(
    quaternion_words: tuple[int, int, int, int], policy: X87Policy
) -> tuple[int, int, int, int]:
    if len(quaternion_words) != 4:
        raise OracleInputError("quaternion-size")
    values = _load_words(quaternion_words)
    squares = tuple(multiply(value, value, policy) for value in values)
    total = add(
        add(add(squares[0], squares[1], policy), squares[2], policy),
        squares[3],
        policy,
    )
    norm = square_root(total, policy)
    if norm.fraction == 0:
        raise OracleInputError("zero-quaternion")
    scale = divide(ONE, norm, policy)
    return tuple(
        _spill(multiply(scale, value, policy), policy) for value in values
    )  # type: ignore[return-value]


def step(input_value: RigidBodyInput, policy: X87Policy) -> StepResult:
    pre_step = derive(input_value, policy)
    unnormalized = tuple(
        euler_lane(state, input_value.dt_bits, derivative, policy)
        for state, derivative in zip(
            input_value.state_words, pre_step.derivative_words
        )
    )
    normalized_quaternion = normalize_quaternion(
        tuple(unnormalized[3:7]), policy  # type: ignore[arg-type]
    )
    post_state = unnormalized[:3] + normalized_quaternion + unnormalized[7:]
    post_auxiliary = calculate_auxiliary(input_value, post_state, policy)
    return StepResult(
        pre_step=pre_step,
        unnormalized_state_words=unnormalized,
        post_state_words=post_state,
        post_step_auxiliary=post_auxiliary,
        cleared_accumulator_words=(0, 0, 0, 0, 0, 0),
    )


def _vector_input(vector_id: str) -> RigidBodyInput:
    common = {
        "mass_bits": 0x3FF0000000000000,
        "body_inverse_inertia_words": IDENTITY_MATRIX,
        "damping_bits": 0,
        "force_words": ZERO3,
        "torque_words": ZERO3,
        "dt_bits": 0x3C449BA6,
    }
    if vector_id == "V1":
        return RigidBodyInput(
            **common,
            state_words=(0, 0, 0, 0x3F800000, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        )
    if vector_id == "V2":
        return RigidBodyInput(
            **{
                **common,
                "mass_bits": 0x4000000000000000,
                "force_words": (0x3F800000, 0xC0000000, 0x40800000),
                "dt_bits": 0x3F000000,
            },
            state_words=(
                0,
                0,
                0,
                0x3F800000,
                0,
                0,
                0,
                0x40000000,
                0xC0800000,
                0x41000000,
                0,
                0,
                0,
            ),
        )
    if vector_id == "V3":
        return RigidBodyInput(
            **{
                **common,
                "body_inverse_inertia_words": (
                    0x40000000,
                    0,
                    0,
                    0,
                    0x40800000,
                    0,
                    0,
                    0,
                    0x41000000,
                ),
            },
            state_words=(
                0,
                0,
                0,
                0x3F000000,
                0x3F000000,
                0x3F000000,
                0x3F000000,
                0,
                0,
                0,
                0x3F800000,
                0x40000000,
                0x40400000,
            ),
        )
    if vector_id == "V4":
        return RigidBodyInput(
            **{**common, "dt_bits": 0x3F800000},
            state_words=(
                0,
                0,
                0,
                0x3F800000,
                0,
                0,
                0,
                0,
                0,
                0,
                0x40000000,
                0,
                0,
            ),
        )
    if vector_id == "V5":
        return RigidBodyInput(
            **{
                **common,
                "damping_bits": 0x3DCCCCCD,
                "torque_words": (0x3F800000,) * 3,
            },
            state_words=(
                0,
                0,
                0,
                0x3F800000,
                0,
                0,
                0,
                0,
                0,
                0,
                0x3F000011,
                0x3F000011,
                0x3F000011,
            ),
        )
    raise OracleInputError("unknown-vector")


def vector_expected(
    vector_id: str, precision: str, rounding: str
) -> dict[str, object]:
    policy = X87Policy(precision, rounding)
    if vector_id in {"V1", "V2", "V3", "V4", "V5"} and rounding != "nearest":
        raise OracleInputError("directed-full-vector-not-published")
    if vector_id == "V6":
        return {
            "dt_bits": 0x39803009,
            "derivative_bits": 0x397FA012,
            "stored_state_bits": euler_lane(
                0x3F800000, 0x39803009, 0x397FA012, policy
            ),
        }

    input_value = _vector_input(vector_id)
    if vector_id == "V3":
        result = derive(input_value, policy)
        return {
            "rotation_words": result.auxiliary.rotation_words,
            "world_inverse_inertia_words": (
                result.auxiliary.world_inverse_inertia_words
            ),
            "angular_velocity_words": result.auxiliary.angular_velocity_words,
        }
    if vector_id == "V5":
        result = derive(input_value, policy)
        return {
            "stored_damping_xy_bits": result.stored_damping_xy_words[0],
            "angular_momentum_derivative_words": result.derivative_words[10:13],
        }

    result = step(input_value, policy)
    if vector_id == "V1":
        return {
            "derivative_words": result.pre_step.derivative_words,
            "post_state_words": result.post_state_words,
            "accumulator_words": result.cleared_accumulator_words,
        }
    if vector_id == "V2":
        return {
            "post_state_words": result.post_state_words,
            "velocity_words": result.post_step_auxiliary.linear_velocity_words,
            "rotation_words": result.post_step_auxiliary.rotation_words,
            "world_inverse_inertia_words": (
                result.post_step_auxiliary.world_inverse_inertia_words
            ),
            "accumulator_words": result.cleared_accumulator_words,
        }
    if vector_id == "V4":
        return {
            "derivative_quaternion_words": result.pre_step.derivative_words[3:7],
            "post_quaternion_words": result.post_state_words[3:7],
        }
    raise OracleInputError("unknown-vector")
