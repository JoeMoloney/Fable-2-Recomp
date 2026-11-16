// Theseus override of StrCmp_Signed_8226D7A8: runs the verified rewrite
// tools/theseus/rewrites/StrCmp_Signed_8226D7A8.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/difftest.py StrCmp_Signed_8226D7A8
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/StrCmp_Signed_8226D7A8.cpp"

THESEUS_OVERRIDE(StrCmp_Signed_8226D7A8)
