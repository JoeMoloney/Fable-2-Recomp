// Theseus override of StringCopyN_822CD828: runs the verified rewrite
// tools/theseus/rewrites/StringCopyN_822CD828.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/difftest.py StringCopyN_822CD828
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/StringCopyN_822CD828.cpp"

THESEUS_OVERRIDE(StringCopyN_822CD828)
