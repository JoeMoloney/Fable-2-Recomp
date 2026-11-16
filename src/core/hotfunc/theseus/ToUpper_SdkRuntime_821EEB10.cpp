// Theseus override of ToUpper_SdkRuntime_821EEB10: runs the verified rewrite
// tools/theseus/rewrites/ToUpper_SdkRuntime_821EEB10.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/gate.py ToUpper_SdkRuntime_821EEB10
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/ToUpper_SdkRuntime_821EEB10.cpp"

THESEUS_OVERRIDE(ToUpper_SdkRuntime_821EEB10)
