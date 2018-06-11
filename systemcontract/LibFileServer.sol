
/**
*@file      LibFileServer.sol
*@author    kelvin
*@time      2016-11-29
*@desc      the defination of LibFileServer
*/

pragma solidity ^0.4.2;

import "LibInt.sol";
import "LibString.sol";


library LibFileServer {
    using LibInt for *;
    using LibString for *;
    using LibFileServer for *;


    /** @brief User state: invalid, valid */
    enum ServerState { INVALID, VALID}

    /** @brief File Structure */
    struct FileServerInfo {
        string      id;
        string      host;
        uint256     port;
        uint256     updateTime;
        string      organization;
        string      position;
        string      group;
        string      info;
        uint256     enable;
        ServerState   state;
    }


    function toJson(FileServerInfo storage _self) internal returns(string _strjson){
        _strjson = "{";

        _strjson = _strjson.concat(_self.id.toKeyValue("id"), ",");
        _strjson = _strjson.concat(_self.host.toKeyValue("host"), ",");
        _strjson = _strjson.concat(uint(_self.port).toKeyValue("port"), ",");
        _strjson = _strjson.concat(uint(_self.updateTime).toKeyValue("updateTime"), ",");
        _strjson = _strjson.concat(_self.position.toKeyValue("position"), ",");
        _strjson = _strjson.concat(_self.group.toKeyValue("group"), ",");
        _strjson = _strjson.concat(_self.organization.toKeyValue("organization"), ",");
        _strjson = _strjson.concat(uint(_self.enable).toKeyValue("enable"), ",");
        _strjson = _strjson.concat(_self.info.toKeyValue("info"), "}");
    }

    function jsonParse(FileServerInfo storage _self, string _json) internal returns(bool) {
        _self.reset();

        _self.id = _json.getStringValueByKey("id");
        _self.host = _json.getStringValueByKey("host");
        _self.port = uint256(_json.getIntValueByKey("port"));
        //_self.updateTime = uint256(_json.getIntValueByKey("updateTime"));
        _self.position = _json.getStringValueByKey("position");
        _self.group = _json.getStringValueByKey("group");
        _self.organization = _json.getStringValueByKey("organization");
        _self.info = _json.getStringValueByKey("info");
        _self.enable = (uint256)(_json.getIntValueByKey("enable"));

        if (bytes(_self.id).length == 0) {
            return false;
        }

        return true;
    }

    function reset(FileServerInfo storage _self) internal {
        _self.id = "";
        _self.host = "";
        _self.port = 0;
        _self.updateTime = 0;
        _self.position = "";
        _self.organization = "";
        _self.info = "";
        _self.group = "";
        _self.enable = 0;

        _self.state = ServerState.INVALID;
    }
}
