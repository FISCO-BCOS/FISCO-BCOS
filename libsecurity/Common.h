#pragma once
#include <libdevcore/Exceptions.h>

namespace dev
{
DEV_SIMPLE_EXCEPTION(KeyCenterAlreadyInit);
DEV_SIMPLE_EXCEPTION(KeyCenterDataKeyError);
DEV_SIMPLE_EXCEPTION(KeyCenterConnectionError);
DEV_SIMPLE_EXCEPTION(KeyCenterCall);
DEV_SIMPLE_EXCEPTION(KeyCenterInitError);
DEV_SIMPLE_EXCEPTION(KeyCenterCloseError);
DEV_SIMPLE_EXCEPTION(EncryptedFileError);
DEV_SIMPLE_EXCEPTION(EncryptedLevelDBEncryptFailed);
DEV_SIMPLE_EXCEPTION(EncryptedLevelDBDecryptFailed);
}  // namespace dev