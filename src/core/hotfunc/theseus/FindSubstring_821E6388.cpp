// Theseus override of FindSubstring_821E6388: runs the verified rewrite
// tools/theseus/rewrites/FindSubstring_821E6388.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/difftest.py FindSubstring_821E6388
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/FindSubstring_821E6388.cpp"

THESEUS_OVERRIDE(FindSubstring_821E6388)
