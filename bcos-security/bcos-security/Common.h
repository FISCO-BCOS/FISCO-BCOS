#pragma once
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Exceptions.h>

// bcos-security is used to storage security

namespace bcos
{

namespace security
{
DERIVE_BCOS_EXCEPTION(KeyCenterAlreadyInit);
DERIVE_BCOS_EXCEPTION(KeyCenterDataKeyError);
DERIVE_BCOS_EXCEPTION(KeyCenterConnectionError);
DERIVE_BCOS_EXCEPTION(KeyCenterCall);
DERIVE_BCOS_EXCEPTION(KeyCenterInitError);
DERIVE_BCOS_EXCEPTION(KeyCenterCloseError);
DERIVE_BCOS_EXCEPTION(EncryptedFileError);
DERIVE_BCOS_EXCEPTION(EncryptedLevelDBEncryptFailed);
DERIVE_BCOS_EXCEPTION(EncryptedLevelDBDecryptFailed);
DERIVE_BCOS_EXCEPTION(EncryptFailed);
DERIVE_BCOS_EXCEPTION(DecryptFailed);

}  // namespace security

}  // namespace bcos