// Theseus override of FlushCache_821D11C8: runs the verified rewrite
// tools/theseus/rewrites/FlushCache_821D11C8.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/gate.py FlushCache_821D11C8
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/FlushCache_821D11C8.cpp"

THESEUS_OVERRIDE(FlushCache_821D11C8)
