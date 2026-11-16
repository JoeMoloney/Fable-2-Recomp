// Theseus override of StringCompare_82CA3920: runs the verified rewrite
// tools/theseus/rewrites/StringCompare_82CA3920.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/gate.py StringCompare_82CA3920
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/StringCompare_82CA3920.cpp"

THESEUS_OVERRIDE(StringCompare_82CA3920)
