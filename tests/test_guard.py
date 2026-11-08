from typing import Literal, Optional

import pytest

from guardian import guard
from guardian._guardian_core import GuardianTypeError


@guard
def add_one(value: int) -> int:
    return value + 1


@guard
def accepts_payload(payload: dict[str, list[int]], mode: Literal["fast", "safe"]) -> tuple[int, ...]:
    return tuple(payload["data"])


@guard
def passthrough(value: int, raw) -> int:
    return value


@guard(check_return=False)
def no_return_check(_: int) -> int:
    return "not-an-int"     #type: ignore[return-value]


@guard
def optional_input(value: Optional[int]) -> Optional[int]:
    return value


def test_guard_rejects_bool_for_exact_int() -> None:
    with pytest.raises(GuardianTypeError):
        add_one(True)


def test_guard_rejects_nested_container_and_literal_mismatch() -> None:
    with pytest.raises(GuardianTypeError):
        accepts_payload({"data": [1, "2"]}, "fast")

    with pytest.raises(GuardianTypeError):
        accepts_payload({"data": [1, 2]}, "turbo")  # type: ignore[arg-type]


def test_guard_accepts_optional_union_none_and_int() -> None:
    assert optional_input(None) is None
    assert optional_input(4) == 4


def test_guard_ignores_unannotated_parameters() -> None:
    assert passthrough(1, object()) == 1


def test_guard_return_validation_and_bypass() -> None:
    with pytest.raises(GuardianTypeError):
        @guard
        def bad_return(_: int) -> int:
            return "bad"       #type: ignore[return-value]

        bad_return(1)

    assert no_return_check(1) == "not-an-int"


def test_guarded_method_binding_works_on_instances() -> None:
    class Worker:
        @guard
        def run(self, count: int) -> int:
            return count

    worker = Worker()
    assert worker.run(2) == 2
    with pytest.raises(GuardianTypeError):
        worker.run("2")  # type: ignore[arg-type]


def test_guard_handles_forward_reference_annotations() -> None:
    @guard
    def parse_age(age: "int") -> "int":
        return age

    assert parse_age(5) == 5
    with pytest.raises(GuardianTypeError):
        parse_age("5")  # type: ignore[arg-type]

