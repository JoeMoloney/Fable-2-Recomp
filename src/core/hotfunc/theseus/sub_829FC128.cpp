// Theseus override of sub_829FC128: runs the verified rewrite
// tools/theseus/rewrites/sub_829FC128.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/gate.py sub_829FC128
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/sub_829FC128.cpp"

THESEUS_OVERRIDE(sub_829FC128)
