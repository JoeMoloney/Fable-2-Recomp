// Theseus override of MemCpy_Aligned_82CA30E8: runs the verified rewrite
// tools/theseus/rewrites/MemCpy_Aligned_82CA30E8.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/gate.py MemCpy_Aligned_82CA30E8
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/MemCpy_Aligned_82CA30E8.cpp"

THESEUS_OVERRIDE(MemCpy_Aligned_82CA30E8)
