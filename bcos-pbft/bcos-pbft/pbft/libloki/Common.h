/** @file Config.h
 *  @author fuchenma
 *  @brief: fuzzer node config variables
 *  @date 20210721
 */

#pragma once
namespace loki
{

// should the loki node send a package without receving a package
const bool SEND_PACKAGE = true;

// send a package after some seconds;
const int SEND_INTERVAL = 5;

// package type
enum PackageType {
    PREPARE_REQ = 0x00,
    SIGN_REQ = 0x01,
    COMMIT_REQ = 0x02,
    VIEWCHANGE_REQ = 0x03,
};

} 


