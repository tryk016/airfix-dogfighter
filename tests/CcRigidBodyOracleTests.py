#!/usr/bin/env python3

from __future__ import annotations

from dataclasses import replace
import importlib.util
from pathlib import Path
import random
import struct
import sys
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY_ROOT / "tools" / "re" / "cc_rigid_body_oracle.py"
SPEC = importlib.util.spec_from_file_location("airfix_cc_rigid_body_oracle", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("unable to load the CcRigidBody oracle")
ORACLE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = ORACLE
SPEC.loader.exec_module(ORACLE)


def binary32_value(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", bits))[0]


def binary32_bits(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", value))[0]


class CcRigidBodyOracleTests(unittest.TestCase):
    def test_published_vectors_match_exact_static_tables(self) -> None:
        identity = ORACLE.IDENTITY_MATRIX
        expected = {
            "V1": {
                "derivative_words": (0, 0, 0, 0x80000000, 0, 0, 0, 0, 0, 0, 0, 0, 0),
                "post_state_words": (0, 0, 0, 0x3F800000, 0, 0, 0, 0, 0, 0, 0, 0, 0),
                "accumulator_words": (0, 0, 0, 0, 0, 0),
            },
            "V2": {
                "post_state_words": (
                    0x3F000000,
                    0xBF800000,
                    0x40000000,
                    0x3F800000,
                    0,
                    0,
                    0,
                    0x40200000,
                    0xC0A00000,
                    0x41200000,
                    0,
                    0,
                    0,
                ),
                "velocity_words": (0x3FA00000, 0xC0200000, 0x40A00000),
                "rotation_words": identity,
                "world_inverse_inertia_words": identity,
                "accumulator_words": (0, 0, 0, 0, 0, 0),
            },
            "V3": {
                "rotation_words": (
                    0,
                    0x3F800000,
                    0,
                    0,
                    0,
                    0x3F800000,
                    0x3F800000,
                    0,
                    0,
                ),
                "world_inverse_inertia_words": (
                    0x41000000,
                    0,
                    0,
                    0,
                    0x40000000,
                    0,
                    0,
                    0,
                    0x40800000,
                ),
                "angular_velocity_words": (0x41000000, 0x40800000, 0x41400000),
            },
            "V4": {
                "derivative_quaternion_words": (0x80000000, 0x3F800000, 0, 0),
                "post_quaternion_words": (0x3F3504F3, 0x3F3504F3, 0, 0),
            },
        }
        for precision in ("pc24", "pc53", "pc64"):
            for vector_id, vector_expected in expected.items():
                with self.subTest(precision=precision, vector=vector_id):
                    self.assertEqual(
                        ORACLE.vector_expected(vector_id, precision, "nearest"),
                        vector_expected,
                    )

        self.assertEqual(
            ORACLE.vector_expected("V5", "pc24", "nearest"),
            {
                "stored_damping_xy_bits": 0x3D4CCCE8,
                "angular_momentum_derivative_words": (
                    0x3F733332,
                    0x3F733332,
                    0x3F733332,
                ),
            },
        )
        for precision in ("pc53", "pc64"):
            self.assertEqual(
                ORACLE.vector_expected("V5", precision, "nearest"),
                {
                    "stored_damping_xy_bits": 0x3D4CCCE8,
                    "angular_momentum_derivative_words": (
                        0x3F733332,
                        0x3F733332,
                        0x3F733331,
                    ),
                },
            )

    def test_v6_distinguishes_precision_and_rounding(self) -> None:
        for precision in ("pc24", "pc53", "pc64"):
            for rounding in ("nearest", "down", "up", "zero"):
                with self.subTest(precision=precision, rounding=rounding):
                    expected = (
                        0x3F800001
                        if rounding == "up"
                        or (rounding == "nearest" and precision == "pc64")
                        else 0x3F800000
                    )
                    self.assertEqual(
                        ORACLE.vector_expected("V6", precision, rounding),
                        {
                            "dt_bits": 0x39803009,
                            "derivative_bits": 0x397FA012,
                            "stored_state_bits": expected,
                        },
                    )

    def test_binary32_exact_values_and_signed_zero_round_trip(self) -> None:
        exact_words = (
            0x00000000,
            0x80000000,
            0x00000001,
            0x80000001,
            0x007FFFFF,
            0x00800000,
            0x3F800000,
            0xBF800000,
            0x7F7FFFFF,
            0xFF7FFFFF,
        )
        for rounding in ("nearest", "down", "up", "zero"):
            for word in exact_words:
                with self.subTest(rounding=rounding, word=f"0x{word:08X}"):
                    self.assertEqual(
                        ORACLE.store_binary32(
                            ORACLE.load_binary32(word), rounding
                        ),
                        word,
                    )

        self.assertEqual(
            ORACLE.store_binary32(
                ORACLE.negate(ORACLE.POSITIVE_ZERO), "nearest"
            ),
            0x80000000,
        )
        self.assertFalse(ORACLE.load_binary64(0).negative_zero)
        self.assertTrue(
            ORACLE.load_binary64(0x8000000000000000).negative_zero
        )

    def test_euler_lane_matches_host_rne_for_pc24_and_pc53(self) -> None:
        randomizer = random.Random(0xCC53E11E)
        finite_words = (
            0,
            0x80000000,
            0x00800000,
            0x3DCCCCCD,
            0x3E800000,
            0x3F000000,
            0x3F800000,
            0x40000000,
            0xBDCCCCCD,
            0xBE800000,
            0xBF000000,
            0xBF800000,
        )
        for iteration in range(500):
            state_bits = randomizer.choice(finite_words)
            dt_bits = randomizer.choice(finite_words)
            derivative_bits = randomizer.choice(finite_words)
            state = binary32_value(state_bits)
            dt = binary32_value(dt_bits)
            derivative = binary32_value(derivative_bits)

            product_pc24 = binary32_value(binary32_bits(dt * derivative))
            expected_pc24 = binary32_bits(product_pc24 + state)
            expected_pc53 = binary32_bits(dt * derivative + state)
            with self.subTest(iteration=iteration):
                self.assertEqual(
                    ORACLE.euler_lane(
                        state_bits,
                        dt_bits,
                        derivative_bits,
                        ORACLE.X87Policy("pc24", "nearest"),
                    ),
                    expected_pc24,
                )
                self.assertEqual(
                    ORACLE.euler_lane(
                        state_bits,
                        dt_bits,
                        derivative_bits,
                        ORACLE.X87Policy("pc53", "nearest"),
                    ),
                    expected_pc53,
                )

    def test_matrix_bit_copy_and_native_row_vector_convention(self) -> None:
        policy = ORACLE.X87Policy("pc53", "nearest")
        source = (
            0x3F800000,
            0x40000000,
            0x40400000,
            0x40800000,
            0x40A00000,
            0x40C00000,
            0x40E00000,
            0x41000000,
            0x41100000,
        )
        self.assertEqual(
            ORACLE.transpose_matrix(ORACLE.transpose_matrix(source)), source
        )
        self.assertEqual(
            ORACLE.multiply_matrices(source, ORACLE.IDENTITY_MATRIX, policy),
            source,
        )
        self.assertEqual(
            ORACLE.multiply_matrices(ORACLE.IDENTITY_MATRIX, source, policy),
            source,
        )
        self.assertEqual(
            ORACLE.multiply_row_vector(
                (0x3F800000, 0x40000000, 0x40400000),
                ORACLE.IDENTITY_MATRIX,
                policy,
            ),
            (0x3F800000, 0x40000000, 0x40400000),
        )

    def test_fail_closed_domain_does_not_claim_native_exception_behavior(self) -> None:
        with self.assertRaisesRegex(ORACLE.OracleInputError, "unsupported-precision"):
            ORACLE.X87Policy("pc80", "nearest")
        with self.assertRaisesRegex(ORACLE.OracleInputError, "unsupported-rounding"):
            ORACLE.X87Policy("pc53", "away")
        for bits in (0x7F800000, 0xFF800000, 0x7FC00000, 0x7FA00001):
            with self.subTest(bits=f"0x{bits:08X}"):
                with self.assertRaisesRegex(
                    ORACLE.OracleInputError, "nonfinite-binary32"
                ):
                    ORACLE.load_binary32(bits)
        for bits in (
            0x7FF0000000000000,
            0xFFF0000000000000,
            0x7FF8000000000000,
        ):
            with self.subTest(bits=f"0x{bits:016X}"):
                with self.assertRaisesRegex(
                    ORACLE.OracleInputError, "nonfinite-binary64"
                ):
                    ORACLE.load_binary64(bits)

        vector = ORACLE._vector_input("V1")
        policy = ORACLE.X87Policy("pc53", "nearest")
        for mass in (0, 0x8000000000000000, 0xBFF0000000000000):
            with self.subTest(mass=f"0x{mass:016X}"):
                with self.assertRaisesRegex(
                    ORACLE.OracleInputError, "mass-not-positive"
                ):
                    ORACLE.step(replace(vector, mass_bits=mass), policy)
        with self.assertRaisesRegex(ORACLE.OracleInputError, "dt-negative"):
            ORACLE.step(replace(vector, dt_bits=0xBC449BA6), policy)
        zero_quaternion = replace(
            vector,
            state_words=(0,) * 13,
        )
        with self.assertRaisesRegex(ORACLE.OracleInputError, "zero-quaternion"):
            ORACLE.step(zero_quaternion, policy)
        with self.assertRaisesRegex(
            ORACLE.OracleInputError, "directed-full-vector-not-published"
        ):
            ORACLE.vector_expected("V5", "pc53", "up")

    def test_inputs_are_immutable_and_repeated_runs_are_deterministic(self) -> None:
        policy = ORACLE.X87Policy("pc64", "nearest")
        vector = ORACLE._vector_input("V2")
        original = vector.state_words
        first = ORACLE.step(vector, policy)
        second = ORACLE.step(vector, policy)
        self.assertEqual(first, second)
        self.assertEqual(vector.state_words, original)
        self.assertNotEqual(first.post_state_words, original)

        randomizer = random.Random(0xCC2A420)
        finite_words = (
            0,
            0x3DCCCCCD,
            0x3E800000,
            0x3F000000,
            0x3F800000,
            0x40000000,
            0xBDCCCCCD,
            0xBE800000,
            0xBF000000,
            0xBF800000,
        )
        for precision in ("pc24", "pc53", "pc64"):
            for rounding in ("nearest", "down", "up", "zero"):
                run_policy = ORACLE.X87Policy(precision, rounding)
                for iteration in range(40):
                    state = (
                        randomizer.choice(finite_words),
                        randomizer.choice(finite_words),
                        randomizer.choice(finite_words),
                        0x3F800000,
                        randomizer.choice(finite_words[:6]),
                        randomizer.choice(finite_words[:6]),
                        randomizer.choice(finite_words[:6]),
                        randomizer.choice(finite_words),
                        randomizer.choice(finite_words),
                        randomizer.choice(finite_words),
                        randomizer.choice(finite_words),
                        randomizer.choice(finite_words),
                        randomizer.choice(finite_words),
                    )
                    candidate = ORACLE.RigidBodyInput(
                        mass_bits=0x3FF0000000000000,
                        body_inverse_inertia_words=ORACLE.IDENTITY_MATRIX,
                        damping_bits=0x3DCCCCCD,
                        state_words=state,
                        force_words=tuple(
                            randomizer.choice(finite_words) for _ in range(3)
                        ),
                        torque_words=tuple(
                            randomizer.choice(finite_words) for _ in range(3)
                        ),
                        dt_bits=0x3C449BA6,
                    )
                    with self.subTest(
                        precision=precision,
                        rounding=rounding,
                        iteration=iteration,
                    ):
                        self.assertEqual(
                            ORACLE.step(candidate, run_policy),
                            ORACLE.step(candidate, run_policy),
                        )

    def test_shape_validation_precedes_arithmetic(self) -> None:
        with self.assertRaisesRegex(
            ORACLE.OracleInputError, "body-inverse-inertia-size"
        ):
            ORACLE.RigidBodyInput(0, (), 0, (0,) * 13, (0,) * 3, (0,) * 3, 0)
        with self.assertRaisesRegex(ORACLE.OracleInputError, "state-size"):
            ORACLE.RigidBodyInput(
                0, ORACLE.IDENTITY_MATRIX, 0, (), (0,) * 3, (0,) * 3, 0
            )
        with self.assertRaisesRegex(ORACLE.OracleInputError, "force-size"):
            ORACLE.RigidBodyInput(
                0, ORACLE.IDENTITY_MATRIX, 0, (0,) * 13, (), (0,) * 3, 0
            )
        with self.assertRaisesRegex(ORACLE.OracleInputError, "torque-size"):
            ORACLE.RigidBodyInput(
                0, ORACLE.IDENTITY_MATRIX, 0, (0,) * 13, (0,) * 3, (), 0
            )
        policy = ORACLE.X87Policy("pc53", "nearest")
        with self.assertRaisesRegex(ORACLE.OracleInputError, "quaternion-size"):
            ORACLE.transform_quaternion_to_matrix((0, 0, 0), policy)
        with self.assertRaisesRegex(ORACLE.OracleInputError, "quaternion-size"):
            ORACLE.normalize_quaternion((0, 0, 0), policy)
        with self.assertRaisesRegex(ORACLE.OracleInputError, "vector-size"):
            ORACLE.multiply_row_vector((0, 0), ORACLE.IDENTITY_MATRIX, policy)
        with self.assertRaisesRegex(ORACLE.OracleInputError, "matrix-size"):
            ORACLE.multiply_row_vector((0, 0, 0), (), policy)
        with self.assertRaisesRegex(ORACLE.OracleInputError, "unsupported-rounding"):
            ORACLE.store_binary32(ORACLE.POSITIVE_ZERO, "away")


if __name__ == "__main__":
    unittest.main()
