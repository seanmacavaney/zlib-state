import os
import sys
from glob import glob
import setuptools

with open("README.md", "r") as fh:
    long_description = fh.read()

include_dirs, libs, lib_dirs = [], [], []

windows = sys.platform.startswith("win")

if windows:
    ZLIB_HOME = os.environ.get("ZLIB_HOME", "C:/Program Files/zlib")
    include_dirs.append(os.path.join(ZLIB_HOME, "include"))
    libs.append('zlib')
    lib_dirs.append(os.path.join(ZLIB_HOME, "lib"))
else:
    libs.append('z')

setuptools.setup(
    name="zlib-state",
    version="0.1.3",
    author="Sean MacAvaney",
    author_email="sean.macavaney@gmail.com",
    description="Low-level interface to the zlib library that enables capturing the decoding state",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/seanmacavaney/zlib-state",
    include_package_data = True,
    packages=setuptools.find_packages(include=['zlib_state']),
    install_requires=[],
    classifiers=[
        'License :: OSI Approved :: MIT License',
    ],
    python_requires='>=3.5',
    ext_modules=[setuptools.Extension("_zlib_state", ["src/zlib_state.c"], libraries=libs, library_dirs=lib_dirs, include_dirs=include_dirs)],
)
