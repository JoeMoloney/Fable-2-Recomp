// Theseus override of GetPointerFromContext_82CC1C70: runs the verified rewrite
// tools/theseus/rewrites/GetPointerFromContext_82CC1C70.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/gate.py GetPointerFromContext_82CC1C70
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/GetPointerFromContext_82CC1C70.cpp"

THESEUS_OVERRIDE(GetPointerFromContext_82CC1C70)
