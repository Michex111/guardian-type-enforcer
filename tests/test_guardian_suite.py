import pytest
from typing import List, Dict, Any
from pydantic import BaseModel, validate_call, ValidationError, ConfigDict
from beartype import beartype
from beartype.roar import BeartypeException

# Guardian Imports
from guardian import guard
from guardian.dataclasses import dataclass as guardian_dataclass
from guardian._guardian_core import GuardianTypeError


# ==========================================
# 1. FIXTURES & TARGET FUNCTIONS
# ==========================================

# --- Standard Functions ---
def raw_process_data(user_id: int, tags: List[str]) -> bool:
    return True

@guard
def guardian_process_data(user_id: int, tags: List[str]) -> bool:
    return True

@beartype
def beartype_process_data(user_id: int, tags: List[str]) -> bool:
    return True

@validate_call
def pydantic_process_data(user_id: int, tags: List[str]) -> bool:
    return True


# --- Dataclasses / Models ---
@guardian_dataclass
class GuardianUser:
    id: int
    username: str

class PydanticUser(BaseModel):
    model_config = ConfigDict(validate_assignment=True)

    id: int
    username: str


# ==========================================
# 2. CORRECTNESS TESTS (Error Catching)
# ==========================================

def test_function_validation_correctness():
    """Ensure all libraries correctly catch invalid function arguments."""
    
    # Guardian should raise its specific C-level exception
    with pytest.raises(GuardianTypeError):
        guardian_process_data("not_an_int", ["tag1"])

    # Beartype raises its AST-generated exception
    with pytest.raises(BeartypeException):
        beartype_process_data("not_an_int", ["tag1"])

    # Pydantic raises ValidationError
    with pytest.raises(ValidationError):
        pydantic_process_data("not_an_int", ["tag1"])


def test_dataclass_assignment_correctness():
    """Ensure intercepted assignment correctly blocks invalid types."""
    
    g_user = GuardianUser(id=1, username="Michex")
    p_user = PydanticUser(id=1, username="Michex")

    # Guardian intercepts via CFieldDescriptor
    with pytest.raises(GuardianTypeError):
        g_user.id = "invalid_string"

    # Pydantic intercepts via custom __setattr__
    with pytest.raises(ValidationError):
        p_user.id = "invalid_string"


# ==========================================
# 3. BENCHMARKS (The Nanosecond Hot Path)
# ==========================================

# --- Function Call Benchmarks ---

def test_bench_raw_baseline(benchmark):
    """Measures the raw Python invocation speed (Zero overhead)."""
    benchmark(raw_process_data, 101, ["system", "backend"])

def test_bench_guardian_function(benchmark):
    """Measures Guardian's C-API Vectorcall overhead."""
    benchmark(guardian_process_data, 101, ["system", "backend"])

def test_bench_beartype_function(benchmark):
    """Measures Beartype's pure-Python AST evaluation overhead."""
    benchmark(beartype_process_data, 101, ["system", "backend"])

def test_bench_pydantic_function(benchmark):
    """Measures Pydantic's Rust FFI boundary overhead."""
    benchmark(pydantic_process_data, 101, ["system", "backend"])


# --- Dataclass Attribute Assignment Benchmarks ---

def test_bench_guardian_dataclass_setattr(benchmark):
    """
    Measures Guardian's CFieldDescriptor speed. 
    This should be blazing fast as it avoids Python frame evaluation.
    """
    user = GuardianUser(id=1, username="Michex")
    
    @benchmark
    def assign_guardian():
        user.id = 999

def test_bench_pydantic_model_setattr(benchmark):
    """
    Measures Pydantic's assignment speed.
    Suffers slightly from __setattr__ dictionary lookups and Rust FFI.
    """
    user = PydanticUser(id=1, username="Michex")
    
    @benchmark
    def assign_pydantic():
        user.id = 999