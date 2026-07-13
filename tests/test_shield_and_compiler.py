from typing import Annotated, Literal

import pytest

from guardian import Shield
from guardian._compiler import (
    OP_DICT,
    OP_LIST,
    OP_LITERAL,
    OP_TUPLE_FIXED,
    OP_TUPLE_VAR,
    compile_rule,
    format_type_name,
)
from guardian._guardian_core import GuardianAccessError, GuardianTypeError


class Account(Shield):
    id: int
    _token: str
    _total: int = 0

    def __init__(self, id: int, token: str):
        self.id = id
        self._token = token
        self.bump()

    def rotate(self, token: str) -> None:
        self._token = token

    @classmethod
    def bump(cls) -> None:
        cls._total += 1


class PremiumAccount(Account):
    points: int

    def __init__(self, id: int, token: str, points: int):
        super().__init__(id=id, token=token)
        self.points = points


def test_shield_validates_constructor_and_attributes() -> None:
    with pytest.raises(GuardianTypeError):
        Account("1", "token")  # type: ignore[arg-type]

    account = Account(1, "token")
    account.id = 3
    assert account.id == 3

    with pytest.raises(GuardianTypeError):
        account.id = "x"  # type: ignore[assignment]


def test_shield_blocks_external_private_writes_but_allows_internal() -> None:
    account = Account(1, "token")
    with pytest.raises(GuardianAccessError):
        account._token = "external"

    account.rotate("internal")
    assert account._token == "internal"


def test_shield_blocks_external_private_class_writes() -> None:
    with pytest.raises(GuardianAccessError):
        Account._total = 99

    before = Account._total
    Account.bump()
    assert Account._total == before + 1


def test_shield_inherits_rules_across_mro() -> None:
    premium = PremiumAccount(1, "token", 10)
    premium.id = 2
    premium.points = 20

    with pytest.raises(GuardianTypeError):
        premium.points = "20"  # type: ignore[assignment]

    with pytest.raises(GuardianTypeError):
        premium.id = "2"  # type: ignore[assignment]


def test_shield_handles_forward_reference_annotations() -> None:
    class Box(Shield):
        amount: "int"

        def __init__(self, amount: "int"):
            self.amount = amount

    box = Box(1)
    with pytest.raises(GuardianTypeError):
        box.amount = "1"  # type: ignore[assignment]


def test_compile_rule_opcodes_for_collections_literals_and_tuples() -> None:
    assert compile_rule(list[int])[0] == OP_LIST
    assert compile_rule(dict[str, int])[0] == OP_DICT
    assert compile_rule(Literal["a", "b"])[0] == OP_LITERAL
    assert compile_rule(tuple[int, ...])[0] == OP_TUPLE_VAR
    assert compile_rule(tuple[int, str])[0] == OP_TUPLE_FIXED


def test_compile_rule_unwraps_annotated() -> None:
    assert compile_rule(Annotated[int, "meta"]) == compile_rule(int)


def test_format_type_name_handles_unions_literals_and_annotated() -> None:
    assert format_type_name(int | str) == "int | str"
    assert format_type_name(Literal["a", 1]) == "Literal['a', 1]"
    assert format_type_name(Annotated[list[int], "meta"]) == "list[int]"

