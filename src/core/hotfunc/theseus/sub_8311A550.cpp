// Theseus override of sub_8311A550: runs the verified rewrite
// tools/theseus/rewrites/sub_8311A550.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/gate.py sub_8311A550
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/sub_8311A550.cpp"

THESEUS_OVERRIDE(sub_8311A550)
