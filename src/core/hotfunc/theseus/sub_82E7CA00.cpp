// Theseus override of sub_82E7CA00: runs the verified rewrite
// tools/theseus/rewrites/sub_82E7CA00.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/gate.py sub_82E7CA00
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/sub_82E7CA00.cpp"

THESEUS_OVERRIDE(sub_82E7CA00)
