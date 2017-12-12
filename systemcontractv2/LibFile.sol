
/**
*@file      LibFile.sol
*@author    kelvin
*@time      2016-11-29
*@desc      the defination of LibFile
*/

pragma solidity ^0.4.2;

import "LibInt.sol";
import "LibString.sol";


library LibFile {
    using LibInt for *;
    using LibString for *;
    using LibFile for *;


    /** @brief User state: invalid, valid */
    enum FileState { FILE_INVALID, FILE_VALID}

    /** @brief File Structure */
    struct FileInfo {
        string      id;
        string      container;
        string      filename;
        uint256     updateTime;
        uint256     size;
        string      owner;
        string      file_hash;/** md5 hash */
        string      src_node;//server node id：    TODO: 考虑删除
        string      node_group;
        uint256     Type;//status
        string      priviliges;
        string      info;
        //uint256     expire;//new add
        FileState   state;
    }


    function toJson(FileInfo storage _self) internal returns(string _strjson){
        _strjson = "{";

        _strjson = _strjson.concat(_self.id.toKeyValue("id"), ",");
        _strjson = _strjson.concat(_self.container.toKeyValue("container"), ",");
        _strjson = _strjson.concat(_self.filename.toKeyValue("filename"), ",");
        _strjson = _strjson.concat(uint(_self.updateTime).toKeyValue("updateTime"), ",");
        _strjson = _strjson.concat(uint(_self.size).toKeyValue("size"), ",");
        _strjson = _strjson.concat(_self.file_hash.toKeyValue("file_hash"), ",");
        _strjson = _strjson.concat(uint(_self.Type).toKeyValue("type"), ",");
        _strjson = _strjson.concat(_self.priviliges.toKeyValue("priviliges"), ",");
        _strjson = _strjson.concat(_self.src_node.toKeyValue("src_node"), ",");
        _strjson = _strjson.concat(_self.node_group.toKeyValue("node_group"), ",");
        _strjson = _strjson.concat(_self.info.toKeyValue("info"), "}");
    }

    function jsonParse(FileInfo storage _self, string _json) internal returns(bool) {
        _self.reset();

        _self.id = _json.getStringValueByKey("id");
        _self.filename = _json.getStringValueByKey("filename");
        _self.container = _json.getStringValueByKey("container");
        _self.size = uint256(_json.getIntValueByKey("size"));
        _self.file_hash = _json.getStringValueByKey("file_hash");
        _self.Type = uint256(_json.getIntValueByKey("type"));
        _self.priviliges = _json.getStringValueByKey("priviliges");
        _self.info = _json.getStringValueByKey("info");
        _self.src_node = _json.getStringValueByKey("src_node");
        _self.node_group = _json.getStringValueByKey("node_group");

        if (bytes(_self.id).length == 0) {
            return false;
        }

        return true;
    }

    function reset(FileInfo storage _self) internal {
        _self.id = "";
        _self.filename = "";
        _self.container = "";
        _self.size = 0;
        _self.file_hash = "";
        _self.node_group = "";
        _self.Type = 0;
        _self.updateTime = 0;
        _self.priviliges = "";
        _self.info = "";
        _self.src_node = "";

        _self.state = FileState.FILE_INVALID;
    }
}
