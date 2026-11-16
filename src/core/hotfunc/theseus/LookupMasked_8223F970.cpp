// Theseus override of LookupMasked_8223F970: runs the verified rewrite
// tools/theseus/rewrites/LookupMasked_8223F970.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/gate.py LookupMasked_8223F970
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/LookupMasked_8223F970.cpp"

THESEUS_OVERRIDE(LookupMasked_8223F970)
