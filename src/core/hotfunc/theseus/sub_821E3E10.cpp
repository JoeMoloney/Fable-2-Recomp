// Theseus override of sub_821E3E10: runs the verified rewrite
// tools/theseus/rewrites/sub_821E3E10.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/difftest.py sub_821E3E10
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/sub_821E3E10.cpp"

THESEUS_OVERRIDE(sub_821E3E10)
