#pragma once
#include <libutilities/Exceptions.h>

namespace bcos
{
DERIVE_BCOS_EXCEPTION(KeyCenterAlreadyInit);
DERIVE_BCOS_EXCEPTION(KeyCenterDataKeyError);
DERIVE_BCOS_EXCEPTION(KeyCenterConnectionError);
DERIVE_BCOS_EXCEPTION(KeyCenterCall);
DERIVE_BCOS_EXCEPTION(KeyCenterInitError);
DERIVE_BCOS_EXCEPTION(KeyCenterCloseError);
DERIVE_BCOS_EXCEPTION(EncryptedFileError);
}  // namespace bcos
