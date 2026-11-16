// Theseus override of HashCstring_82188CF0: runs the verified rewrite
// tools/theseus/rewrites/HashCstring_82188CF0.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/difftest.py HashCstring_82188CF0
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/HashCstring_82188CF0.cpp"

THESEUS_OVERRIDE(HashCstring_82188CF0)
