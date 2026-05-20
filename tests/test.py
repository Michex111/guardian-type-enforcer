import pytest
from typing import List, Dict, Union, Any, Optional

from guardian import guard, deepguard, Shield
from guardian.dataclasses import dataclass, validator, FrozenInstanceError, asdict
from guardian._guardian_core import GuardianTypeError, GuardianAccessError

# ==========================================
# SCENARIO 1: API Payload Processing (Functions)
# ==========================================

@guard
def process_payment(user_id: Union[int, str], amount: float, metadata: Dict[str, Any]) -> bool:
    if amount <= 0:
        return False
    return True

@guard(check_return=False)
def fire_and_forget_webhook(payload: dict) -> int:
    # Simulating a function where we don't care about validating the return type 
    # to save nanoseconds on the hot path.
    return "this_is_a_string_but_return_is_not_checked"

def test_api_boundary_validation():
    """Test standard @guard intercepts invalid API payloads instantly."""
    
    # 1. Valid cases
    assert process_payment(101, 50.5, {"currency": "USD"}) is True
    assert process_payment("USR-99", 12.0, {}) is True

    # 2. Invalid Input Types
    with pytest.raises(GuardianTypeError, match="expected float"):
        process_payment(101, "50.5", {})  # amount is string

    with pytest.raises(GuardianTypeError, match="expected dict"):
        process_payment(101, 50.5, metadata=[])  # metadata is list

def test_guard_check_return_bypass():
    """Test that check_return=False successfully bypasses output validation."""
    # This should NOT raise a GuardianTypeError, even though it returns a string instead of int
    result = fire_and_forget_webhook({"event": "ping"})
    assert result == "this_is_a_string_but_return_is_not_checked"


# ==========================================
# SCENARIO 2: Domain Entities (Shield & Encapsulation)
# ==========================================

class BankAccount(Shield):
    balance: float
    _is_active: bool
    
    # Class-level attribute protected by ShieldMeta
    _total_accounts: int = 0

    def __init__(self, initial_balance: float):
        self.balance = initial_balance
        self._is_active = True
        self.increment_accounts()

    @classmethod
    def increment_accounts(cls):
        """Internal class method mutating a private class attribute (Allowed)"""
        cls._total_accounts += 1

    def close_account(self):
        """Internal instance method mutating a private instance attribute (Allowed)"""
        self._is_active = False

def test_shield_encapsulation_and_access():
    """Test public read / protected write native CPython encapsulation."""
    
    account = BankAccount(100.0)

    # 1. Public Assignment Validation
    account.balance = 200.0
    assert account.balance == 200.0
    
    with pytest.raises(GuardianTypeError):
        account.balance = "200" # Invalid type

    # 2. Private Attribute Read (Allowed natively)
    assert account._is_active is True
    assert BankAccount._total_accounts > 0

    # 3. Private Attribute External Write (Blocked by C-Extension)
    with pytest.raises(GuardianAccessError, match="External access denied"):
        account._is_active = False
        
    with pytest.raises(GuardianAccessError, match="Cannot modify protected/private class attribute"):
        BankAccount._total_accounts = 999

    # 4. Private Attribute Internal Write (Allowed by C-Extension frame walker)
    account.close_account()
    assert account._is_active is False


# ==========================================
# SCENARIO 3: Complex Configurations (Dataclasses)
# ==========================================

@dataclass(frozen=True)
class DatabaseConfig:
    host: str
    port: int

@dataclass
class Microservice:
    service_name: str
    db: DatabaseConfig
    replicas: int = 1

    @validator("replicas")
    def validate_replicas(cls, value):
        if value < 1:
            raise ValueError("Must have at least 1 replica")
        return value

def test_dataclass_validation_and_serialization():
    """Test nested C-descriptor assignment, custom validators, and fast dumping."""
    
    db = DatabaseConfig(host="localhost", port=5432)
    service = Microservice(service_name="auth_service", db=db)

    # 1. Initialization and Type Checking
    assert service.replicas == 1
    
    with pytest.raises(GuardianTypeError):
        Microservice(service_name="auth", db="not_a_config_object")

    # 2. Assignment Validation via CFieldDescriptor
    with pytest.raises(GuardianTypeError):
        service.replicas = "3"

    # 3. Custom Validator Enforcement
    with pytest.raises(ValueError, match="Must have at least 1 replica"):
        service.replicas = 0

    # 4. Frozen Enforcement (Standard dataclass behavior)
    with pytest.raises(FrozenInstanceError):
        db.port = 5433

    # 5. Recursive Model Dumping
    dump = asdict(service)
    assert dump == {
        "service_name": "auth_service",
        "replicas": 1,
        "db": {
            "host": "localhost",
            "port": 5432
        }
    }


# ==========================================
# SCENARIO 4: The Internal Auditor (Deepguard)
# ==========================================

@guard
def shallow_calculation(multiplier: float) -> float:
    multiplier = "I am mutating this internally to a string"
    # Fails downstream because we return the string, but internal mutation is ignored
    return multiplier 

@deepguard
def audited_calculation(multiplier: float) -> float:
    multiplier = "I am mutating this internally to a string"
    return 10.0 # Return is valid, but internal state was corrupted

def test_deepguard_tracing():
    """Test the PyEval_SetProfile tracing catches internal frame mutations."""
    
    # 1. Standard Guard catches the bad return, but doesn't trace internal state
    with pytest.raises(GuardianTypeError, match="return"):
        shallow_calculation(5.0)

    # 2. Deepguard intercepts the local frame and catches the internal variable mutation
    with pytest.raises(GuardianTypeError, match="Variable 'multiplier' expected float"):
        audited_calculation(5.0)