// Theseus override of HashLookupByKey_82228570: runs the verified rewrite
// tools/theseus/rewrites/HashLookupByKey_82228570.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/gate.py HashLookupByKey_82228570
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/HashLookupByKey_82228570.cpp"

THESEUS_OVERRIDE(HashLookupByKey_82228570)
