
/** @file Host.cpp
 * Kept as a thin translation unit: Host<DecoderT, SocketT> is a class template now, so all member
 * definitions live in Host.h (below the class declaration) and are instantiated by
 * the TUs that use a concrete Host specialization (the gateway: libp2p's P2PDecoder).
 */

#include "bcos-gateway/libnetwork/Host.h"
