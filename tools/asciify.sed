# Accented Latin letters to their bare ASCII, for text going into a DOS
# archive.
#
# iconv would be the obvious tool and is the wrong one. Its //TRANSLIT spells
# the accent out rather than dropping it, so "Vinícius Ferrão" comes back as
# "Vin'icius Ferr~ao", and //IGNORE deletes the letter entirely and gives
# "Vincius". Neither is the name. This table gives "Vinicius Ferrao", which is
# what the program itself draws on screen and what src/version.h explains at
# length: a DOS machine renders a byte above 7Eh according to whichever code
# page it booted with, and 437, 850 and 860 disagree about exactly these.
#
# Only the letters this project's own text uses are here. That is deliberate.
# Whatever generates a DOS file with this must assert the result is seven-bit
# ASCII afterwards, so an unmapped letter stops a release rather than shipping
# two wrong glyphs in somebody's name.
s/á/a/g
s/à/a/g
s/â/a/g
s/ã/a/g
s/ä/a/g
s/é/e/g
s/è/e/g
s/ê/e/g
s/í/i/g
s/ì/i/g
s/î/i/g
s/ó/o/g
s/ò/o/g
s/ô/o/g
s/õ/o/g
s/ö/o/g
s/ú/u/g
s/ù/u/g
s/û/u/g
s/ü/u/g
s/ç/c/g
s/ñ/n/g
s/Á/A/g
s/À/A/g
s/Â/A/g
s/Ã/A/g
s/Ä/A/g
s/É/E/g
s/È/E/g
s/Ê/E/g
s/Í/I/g
s/Ì/I/g
s/Î/I/g
s/Ó/O/g
s/Ò/O/g
s/Ô/O/g
s/Õ/O/g
s/Ö/O/g
s/Ú/U/g
s/Ù/U/g
s/Û/U/g
s/Ü/U/g
s/Ç/C/g
s/Ñ/N/g
