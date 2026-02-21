import sys
from setuptools import setup, Extension

# Define OS-specific flags
if sys.platform == "win32":
    compile_args = ["/O2"]
else:
    # Linux and Mac use these
    compile_args = ["-O3", "-flto", "-Wall"]

guardian_core = Extension(
    "guardian._guardian_core",
    sources=["src/_guardian_core.c"],
    extra_compile_args=compile_args,
)

setup(
    name="guardian-type-enforcer",
    version="2.0.3", # Increment version for the new attempt
    ext_modules=[guardian_core],
    packages=["guardian"],
    long_description=open("README.md", encoding="utf-8").read(),
    long_description_content_type="text/markdown",
    license="MIT",
)