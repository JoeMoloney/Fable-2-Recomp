// Theseus override of SwitchDispatch_82C8DAE8: runs the verified rewrite
// tools/theseus/rewrites/SwitchDispatch_82C8DAE8.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/gate.py SwitchDispatch_82C8DAE8
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/SwitchDispatch_82C8DAE8.cpp"

THESEUS_OVERRIDE(SwitchDispatch_82C8DAE8)
