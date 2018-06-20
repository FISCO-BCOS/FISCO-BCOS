/**
*@file      FileServerManager.sol
*@author    kelvin
*@time      2017-01-13
*@desc      the defination of FileServerManager contract
*/

pragma solidity ^0.4.2;

import "LibFileServer.sol";
import "OwnerNamed.sol";


/** FileServerManager contract */
contract FileServerManager is OwnerNamed {
	using LibString for *;
    using LibInt for *;
    using LibFileServer for *;

    enum ServerError {
        NO_ERROR,
        JSON_INVALID,
        ID_CONFLICTED,
        NAME_EMPTY,
        ID_NOT_EXISTS,
        NO_PERMISSION
    }

    mapping(string=>LibFileServer.FileServerInfo)   fileServerInfoMap;//<id, serverInfo>
    string[]                            fileServerIdList;
    string[]							fileServerIdListTemp;
    LibFileServer.FileServerInfo 				serverInfo;

    //event to caller 
    event Notify(uint256 _errno, string _info);

    function FileServerManager() {
    }

	/*** insert a file server */
    function insert(string _json) public returns(bool) {
    	if (!serverInfo.jsonParse(_json)) {
            errno = uint256(ServerError.JSON_INVALID);
            log("bad server json !");
            Notify(errno, "bad server json");
    		return false;
    	}

    	//check if exists
    	if (fileServerInfoMap[serverInfo.id].state != LibFileServer.ServerState.INVALID){
    		errno = uint256(ServerError.ID_NOT_EXISTS);
    		log("the server already exists !");
    		Notify(errno, "the server already exists");
    		return false;
    	}

    	serverInfo.state = LibFileServer.ServerState.VALID;
        serverInfo.updateTime = now * 1000;

    	//add to list
   		fileServerIdList.push(serverInfo.id);

    	//add to mapping
    	fileServerInfoMap[serverInfo.id] = serverInfo;

    	errno = uint256(ServerError.NO_ERROR);
    	log("insert server success !");
    	Notify(errno, "insert server success");
    	return true;
    }

    /** 
    ***delete a server 
	**
    */
    function deleteById(string _serverId) public returns(bool) {
     	if (fileServerInfoMap[_serverId].state == LibFileServer.ServerState.INVALID) {
     		errno = uint256(ServerError.ID_NOT_EXISTS);
     		log("delete server not exists");
     		Notify(errno, "delete server not exists");
     		return true;
     	}

     	delete fileServerIdListTemp;
     	for (uint i = 0 ; i < fileServerIdList.length; ++i) {
     		if (fileServerIdList[i].equals(_serverId)) {
     			continue;
     		}

     		fileServerIdListTemp.push(fileServerIdList[i]);
     	}

     	delete fileServerIdList;
     	for (i = 0 ; i < fileServerIdListTemp.length; ++i) {
     		fileServerIdList.push(fileServerIdListTemp[i]);
     	}

     	fileServerInfoMap[_serverId].state = LibFileServer.ServerState.INVALID;
     	errno = uint256(ServerError.NO_ERROR);
     	log("delete server success, id: ", _serverId);
     	Notify(errno, "delete server success");
     	return true;
    }

    /*** update server info */
    function update(string _json) public returns(bool) {
    	serverInfo.reset();
    	if (!serverInfo.jsonParse(_json)) {
    		errno = uint256(ServerError.JSON_INVALID);
     		log("bad file json");
     		Notify(errno, "bad file json");
     		return false;
    	}

    	if (fileServerInfoMap[serverInfo.id].state == LibFileServer.ServerState.INVALID) {
     		errno = uint256(ServerError.ID_NOT_EXISTS);
     		log("update file not exists");
     		Notify(errno, "update file not exists");
     		return false;
     	}

     	//update info
     	serverInfo.state = LibFileServer.ServerState.VALID;
        serverInfo.updateTime = now * 1000;
        
     	fileServerInfoMap[serverInfo.id] = serverInfo;
     	errno = uint256(ServerError.NO_ERROR);
     	log("update file success");
     	Notify(errno, "update file success");
     	return true;
    }

    /** enable or disable the file service of the node, 0 is disable, 1 or else is enable */
    function enable(string _serverId, uint256 _enable) public returns(bool) {
        for (uint index = 0; index < fileServerIdList.length; index++) {
            if (fileServerInfoMap[fileServerIdList[index]].id.equals(_serverId) && fileServerInfoMap[_serverId].state != LibFileServer.ServerState.INVALID) {
                fileServerInfoMap[_serverId].enable = _enable;
                if (_enable == 0) {
                    log("disable the server: ", _serverId, "FileServerManager");
                }
                else {
                    log("enable the server: ", _serverId, "FileServerManager");
                }

                errno = uint256(ServerError.NO_ERROR);
                Notify(errno, "enable server success");
                return true;
            } 
        }

        log("enable server fail, ", _serverId, "FileServerManager");
        Notify(errno, "enable server fail");
        return false;
    }

    /*** find a file by fileId and container */
    function find(string _serverId) constant public returns(string _ret) {
     	_ret = "{\"ret\":0,\"data\":{";
        uint count = 0;

        if (fileServerInfoMap[_serverId].state != LibFileServer.ServerState.INVALID) {
            count = 1;
            _ret = _ret.concat(count.toKeyValue("total"), ",\"items\":[");
            _ret = _ret.concat(fileServerInfoMap[_serverId].toJson());
        } else {
            _ret = _ret.concat(count.toKeyValue("total"), ",\"items\":[");
        }

        _ret = _ret.concat("]}}");
    }

    /*** find a file by fileId and container */
    function isServerEnable(string _serverId) constant public returns(uint256 _ret) {
        if (fileServerInfoMap[_serverId].state != LibFileServer.ServerState.INVALID && fileServerInfoMap[_serverId].enable != 0) {
            _ret = 1;
        } else {
            _ret = 0;
        }
    }

    /** get the count of all servers includes disable servers */
    function getCount() constant public returns(uint256 _total) {
        _total = fileServerIdList.length;
    }

    /** list all the servers info */
    function listAll() constant public returns(string _ret){
        _ret = "{\"ret\":0,\"data\":{";
        _ret = _ret.concat(fileServerIdList.length.toKeyValue("total"), ",\"items\":[");
        uint total = 0;
        for (uint index = 0; index < fileServerIdList.length; index++) {
            if (fileServerInfoMap[fileServerIdList[index]].state != LibFileServer.ServerState.INVALID) {
                if (total > 0) {
                    _ret = _ret.concat(",");
                }

                total = total + 1;
                _ret = _ret.concat(fileServerInfoMap[fileServerIdList[index]].toJson());
            }
        }
        
        _ret = _ret.concat("]}}");
    }

    /** list server by group name */
    function listByGroup(string _group) constant public returns(string _ret){
        _ret = "{\"ret\":0,\"data\":{";
        _ret = _ret.concat(fileServerIdList.length.toKeyValue("total"), ",\"items\":[");
        uint total = 0;
        for (uint index = 0; index < fileServerIdList.length; index++) {
            if (fileServerInfoMap[fileServerIdList[index]].state != LibFileServer.ServerState.INVALID && fileServerInfoMap[fileServerIdList[index]].group.equals(_group)) {
                if (total > 0) {
                    _ret = _ret.concat(",");
                }

                total = total + 1;
                _ret = _ret.concat(fileServerInfoMap[fileServerIdList[index]].toJson());
            }
        }
        
        _ret = _ret.concat("]}}");
    }

    function findIdByHostPort(string _host, uint256 _port) constant public returns(string _ret) {
        for (uint index = 0; index < fileServerIdList.length; index++) {
            if (fileServerInfoMap[fileServerIdList[index]].state != LibFileServer.ServerState.INVALID && fileServerInfoMap[fileServerIdList[index]].host.equals(_host) && fileServerInfoMap[fileServerIdList[index]].port == _port) {
                _ret = fileServerInfoMap[fileServerIdList[index]].id;
                return;
            }
        }

        _ret = "";
        return;
    }
}