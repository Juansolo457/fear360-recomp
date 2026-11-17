"""Helpers for pc/tools/convert_ia.py: Xbox 360 F.E.A.R. world files to PC worlds.

  binio      byte order helpers and the copying cursor the converters share
  pcfiles    the PC game's archives (LTAR) and the index of what the PC has
  objects    the object list all three file kinds share
  classdefs  class property tables read from the PC GameServer.dll
  core       .wld: collision, physics, precache list, blind data
  render     .wldclnt: render meshes and sector data
  convert    object conversion, content fixes, one whole map
  compare    section by section comparison with another converter's output

The file formats were learned by reading the installer of the mod "F.E.A.R. Exclusive
Console Content"; the code here is our own.
"""


class WorldError(Exception):
    """An input file is not what the converter expects."""
