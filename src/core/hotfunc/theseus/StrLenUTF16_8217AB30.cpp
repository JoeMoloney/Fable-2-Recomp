// Theseus override of StrLenUTF16_8217AB30: runs the verified rewrite
// tools/theseus/rewrites/StrLenUTF16_8217AB30.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/difftest.py StrLenUTF16_8217AB30
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/StrLenUTF16_8217AB30.cpp"

THESEUS_OVERRIDE(StrLenUTF16_8217AB30)
