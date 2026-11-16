// Theseus override of ToLower_Ascii_821EE9E8: runs the verified rewrite
// tools/theseus/rewrites/ToLower_Ascii_821EE9E8.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/gate.py ToLower_Ascii_821EE9E8
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/ToLower_Ascii_821EE9E8.cpp"

THESEUS_OVERRIDE(ToLower_Ascii_821EE9E8)
