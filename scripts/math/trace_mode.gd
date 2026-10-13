class_name TraceMode
extends RefCounted

# Trace mode of a TracePath: PHYSICAL fires an
# effect only on a full block; PLANNED fires on the next plan entry (id+side).

const PHYSICAL := 0
const PLANNED := 1
