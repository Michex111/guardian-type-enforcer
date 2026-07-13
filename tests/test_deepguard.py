import pytest

from guardian import deepguard
from guardian._guardian_core import (
    GuardianInitializationError,
    GuardianTypeError,
)


@deepguard
def local_type_corruption(seed: int) -> int:
    local_value: int = seed
    local_value = "invalid"
    return seed


@deepguard
def uninitialized_local(flag: bool) -> int:
    pending: int
    if flag:
        pending = 1
    return 1


@deepguard(check_return=False)
def deepguard_no_return_check(x: int) -> int:
    return "wrong"


def test_deepguard_detects_local_mutation_type_corruption() -> None:
    with pytest.raises(GuardianTypeError, match="Variable 'local_value' expected int"):
        local_type_corruption(1)


def test_deepguard_detects_uninitialized_annotated_locals() -> None:
    with pytest.raises(GuardianInitializationError, match="never initialized"):
        uninitialized_local(False)


def test_deepguard_can_skip_return_check() -> None:
    assert deepguard_no_return_check(1) == "wrong"

