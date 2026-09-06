# t8n golden corpus (vendored subset) — provenance

Vendored from FISCO-BCOS/op-stack-e2e-tests @ 1f1fd4d9a2e8d76a7f98231ad0a9606d01f84a8f
(op-geth v1.101702.2 reference corpus; see DIVERGENCES.md alongside this file for the
block-execution divergence matrix). Only the vectors the OpEngineServiceExecParityTest
executes are vendored — the golden/engine files are op-geth t8n outputs and are
the op-geth oracle for this suite. To refresh or extend: fetch the upstream repo
at the pin above and re-verify the SHA256 sums below.

## SHA256

ab54499af927eb8cfe1a9ede8ca7097797435028545a5d63e15351d8a5873fe3  vectors/isthmus_deposit_only.json
3cc80b4de6fd7787c548033e55fc9114c5a704fe83701dac521281f59430656d  vectors/jovian_deposit_only.json
a21f92b165d258633d50765ccb758005d8ce6b127838f122bcb8126740b3d4df  vectors/jovian_transfer_multi.json
8f35b0526c4240981bf101b1f0435b36eb7d75f719775bc9e36250b3970ca3a3  golden/engine/isthmus_deposit_only.golden.json
0c7c087545530f34560d632e9260147bbb6f62021110f90fba154ab079de2d72  golden/engine/jovian_deposit_only.golden.json
5c4fe8f5b461987f92aad640e3e2892c5b458b7e98c0113dd37ea238207366c7  golden/engine/jovian_transfer_multi.golden.json
f11545e56ee88f4891bcfa4960f8eccf473e290a0afe2f37d75fb87369573bf9  DIVERGENCES.md
