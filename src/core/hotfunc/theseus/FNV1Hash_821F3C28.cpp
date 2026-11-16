// Theseus override of FNV1Hash_821F3C28: runs the verified rewrite
// tools/theseus/rewrites/FNV1Hash_821F3C28.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/difftest.py FNV1Hash_821F3C28
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/FNV1Hash_821F3C28.cpp"

THESEUS_OVERRIDE(FNV1Hash_821F3C28)
