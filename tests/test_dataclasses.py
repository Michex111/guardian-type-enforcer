import pytest

from guardian._guardian_core import GuardianTypeError
from guardian._serialization import dump_dict
from guardian.dataclasses import FrozenInstanceError, asdict, dataclass, validator


@dataclass
class ChildConfig:
    enabled: bool


@dataclass
class ServiceConfig:
    name: str
    retries: int
    child: ChildConfig

    @validator("name")
    def normalize_name(cls, value: str) -> str:
        return value.strip().lower()

    @validator("retries")
    def retries_non_negative(cls, value: int) -> int:
        if value < 0:
            raise ValueError("retries must be >= 0")
        return value


@dataclass(frozen=True)
class FrozenConfig:
    timeout: int


def test_dataclass_validates_init_and_assignment() -> None:
    cfg = ServiceConfig(name=" API ", retries=2, child=ChildConfig(enabled=True))
    assert cfg.name == "api"

    with pytest.raises(GuardianTypeError):
        ServiceConfig(name="svc", retries=1, child="bad")  # type: ignore[arg-type]

    with pytest.raises(GuardianTypeError):
        cfg.retries = "3"  # type: ignore[assignment]


def test_dataclass_custom_validator_error_propagates() -> None:
    cfg = ServiceConfig(name="svc", retries=1, child=ChildConfig(enabled=True))
    with pytest.raises(ValueError, match="retries must be >= 0"):
        cfg.retries = -1


def test_frozen_dataclass_raises_guardian_frozen_error() -> None:
    cfg = FrozenConfig(timeout=5)
    with pytest.raises(FrozenInstanceError):
        cfg.timeout = 10


def test_asdict_recursive_filters_and_overrides() -> None:
    cfg = ServiceConfig(name="svc", retries=1, child=ChildConfig(enabled=True))
    dumped = asdict(cfg, exclude={"retries"}, child={"enabled": False})
    assert dumped == {"name": "svc", "child": {"enabled": False}}


def test_dump_dict_prefers_custom_serializer() -> None:
    class CustomSerializable:
        def __guardian_serialize__(self) -> dict[str, str]:
            return {"custom": "yes"}

    assert dump_dict(CustomSerializable()) == {"custom": "yes"}

