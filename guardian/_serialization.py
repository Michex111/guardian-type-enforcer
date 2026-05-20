import dataclasses
from typing import Optional


def dump_dict(obj, include: Optional[set] = None, exclude: Optional[set] = None, recursive: bool = True, **overrides) -> dict:
    """
    Serializes a guardian dataclass to a dictionary.
    Prioritizes __guardian_serialize__ hooks, falling back to standard extraction.
    """
    if hasattr(obj, "__guardian_serialize__"):
        return obj.__guardian_serialize__()

    result = {}
    for field in dataclasses.fields(obj):
        name = field.name

        if include and name not in include:
            continue
        if exclude and name in exclude:
            continue

        # Handle temporary overrides
        if name in overrides:
            result[name] = overrides[name]
            continue

        value = getattr(obj, name)

        if recursive and dataclasses.is_dataclass(value):
            result[name] = dump_dict(value, recursive=True)
        elif recursive and isinstance(value, list) and len(value) > 0 and dataclasses.is_dataclass(value[0]):
            result[name] = [dump_dict(item, recursive=True) for item in value]
        else:
            result[name] = value

    # Apply any overrides that weren't standard fields
    for k, v in overrides.items():
        if k not in result:
            result[k] = v

    return result