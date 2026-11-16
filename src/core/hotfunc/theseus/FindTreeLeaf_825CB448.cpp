// Theseus override of FindTreeLeaf_825CB448: runs the verified rewrite
// tools/theseus/rewrites/FindTreeLeaf_825CB448.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/difftest.py FindTreeLeaf_825CB448
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/FindTreeLeaf_825CB448.cpp"

THESEUS_OVERRIDE(FindTreeLeaf_825CB448)
