// Theseus override of ComputeSpecialFunction_82239E88: runs the verified rewrite
// tools/theseus/rewrites/ComputeSpecialFunction_82239E88.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/difftest.py ComputeSpecialFunction_82239E88
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/ComputeSpecialFunction_82239E88.cpp"

THESEUS_OVERRIDE(ComputeSpecialFunction_82239E88)
