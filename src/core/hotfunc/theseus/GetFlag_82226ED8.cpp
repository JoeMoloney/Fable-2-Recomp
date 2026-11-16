// Theseus override of GetFlag_82226ED8: runs the verified rewrite
// tools/theseus/rewrites/GetFlag_82226ED8.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/gate.py GetFlag_82226ED8
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/GetFlag_82226ED8.cpp"

THESEUS_OVERRIDE(GetFlag_82226ED8)
