// Theseus override of HashTable_Find2_829FC630: runs the verified rewrite
// tools/theseus/rewrites/HashTable_Find2_829FC630.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/gate.py HashTable_Find2_829FC630
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/HashTable_Find2_829FC630.cpp"

THESEUS_OVERRIDE(HashTable_Find2_829FC630)
