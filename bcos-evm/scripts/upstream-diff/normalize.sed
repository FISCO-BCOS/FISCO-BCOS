# Strip line comments (// …) for drift comparison only — not a C++ preprocessor.
s|//.*$||
# Qualifier normalization: ref uses fully-qualified evmone::state names.
s/evmone::state:://g
s/evmone:://g
s/evmc:://g
# OP thin-layer renames that must not count as drift.
s/OpHost/Host/g
s/props\.props/tx_props/g
s/m_state/state/g
s/m_rev/rev/g
s/[[:space:]]+$//
/^[[:space:]]*$/d
