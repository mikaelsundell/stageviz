# SPDX-License-Identifier: BSD-3-Clause
"""Sample Stageviz conversion plugins; import fileformats and call register()."""
from .objformat import register

__all__ = ['register']
