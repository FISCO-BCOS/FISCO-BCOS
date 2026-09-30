/**
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file L2ConfigLoader.cpp
 * @brief Translation unit placeholder for L2ConfigLoaderImpl.
 *
 * L2ConfigLoaderImpl is a header-only template (L2ConfigLoader.h) parameterized on the
 * concrete state storage type; this file only gives bcos-framework a TU that anchors the
 * header. The production instantiation is OpSystemConfigLoader in
 * libinitializer/OpSystemConfigLoader.h.
 *
 * This TU is part of the bcos-framework unity build, which the Windows CI job compiles with
 * MSVC. MSVC 14.51 rejects EVMAccount.h's constexpr system-address constant in that TU
 * (C7595/C2131 at EVMAccount.h:71/89/101), so the include graph below L2ConfigLoader.h must
 * stay free of EVMAccount.h: the SystemConfig table name is passed into the loader by its
 * caller (L2SystemConfigTable.h) rather than derived here.
 */
#include "L2ConfigLoader.h"
