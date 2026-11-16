// Theseus override of GetIndexedPointer_82BC5448: runs the verified rewrite
// tools/theseus/rewrites/GetIndexedPointer_82BC5448.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/gate.py GetIndexedPointer_82BC5448
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/GetIndexedPointer_82BC5448.cpp"

THESEUS_OVERRIDE(GetIndexedPointer_82BC5448)
