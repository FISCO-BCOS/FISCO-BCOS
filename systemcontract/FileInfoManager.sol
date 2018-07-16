/**
*@file      FileInfoManager.sol
*@author    kelvin
*@time      2017-02-13
*@desc      文件信息管理合约
*/

pragma solidity ^0.4.2;

import "OwnerNamed.sol";
import "LibFile.sol";


contract FileInfoManager is OwnerNamed {
    using LibString for *;
    using LibInt for *;
    using LibFile for *;
    

    mapping(string=>LibFile.FileInfo)   fileInfoMap;//<id, FileInfo>
    string[]                            fileIdList;
    string[]							fileIdListTemp;
    LibFile.FileInfo 					fileInfo;
    uint                                defaultPageSize;

    enum FileError {
        NO_ERROR,
        JSON_INVALID,
        ID_CONFLICTED,
        NAME_EMPTY,
        ID_NOT_EXISTS,
        NO_PERMISSION,
        CONTAINER_NOT_EXISTS
    }

    //event to caller 
    event Notify(uint _errno, string _info);

    function FileInfoManager() {
        defaultPageSize = 16;
    }

	/** insert a file */
    function insert(string _fileJson) public returns(bool) {
    	if (!fileInfo.jsonParse(_fileJson)) {
            errno = uint256(FileError.JSON_INVALID);
            log("bad json file !");
            Notify(errno, "bad file json");
            return false;
    	}

    	//check if exists
    	if (fileInfoMap[fileInfo.id].state != LibFile.FileState.FILE_INVALID){
    		errno = uint256(FileError.ID_NOT_EXISTS);
    		log("the file already exists !");
    		Notify(errno, "the file already exists");
    		return false;
    	}

    	fileInfo.state = LibFile.FileState.FILE_VALID;
        fileInfo.updateTime = now * 1000;

    	//add to list
   		fileIdList.push(fileInfo.id);

    	//add to mapping
    	fileInfoMap[fileInfo.id] = fileInfo;

    	errno = uint256(FileError.NO_ERROR);
    	log("insert file success !");
    	Notify(errno, "insert file success");
    	return true;
    }

    /** 
    ** delete a file 
	**
    */
    function deleteById(string _fileId) public returns(bool) {
        if (fileInfoMap[_fileId].state == LibFile.FileState.FILE_INVALID) {
     		errno = uint256(FileError.ID_NOT_EXISTS);
     		log("delete file not exists");
     		Notify(errno, "delete file not exists");
     		return true;
     	}

     	delete fileIdListTemp;
     	for (uint i = 0 ; i < fileIdList.length; ++i) {
     		if (fileIdList[i].equals(_fileId)) {
     			continue;
     		}

     		fileIdListTemp.push(fileIdList[i]);
     	}

     	delete fileIdList;
     	for (i = 0 ; i < fileIdListTemp.length; ++i) {
     		fileIdList.push(fileIdListTemp[i]);
     	}

     	fileInfoMap[_fileId].state = LibFile.FileState.FILE_INVALID;
     	errno = uint256(FileError.NO_ERROR);
     	log("delete file success, id: ", _fileId);
     	Notify(errno, "delete file success");
     	return true;
    }

    /**  update file info */
    function update(string _fileJson) public returns(bool) {
    	fileInfo.reset();
    	if (!fileInfo.jsonParse(_fileJson)) {
    		errno = uint256(FileError.JSON_INVALID);
     		log("bad file json");
     		Notify(errno, "bad file json");
     		return false;
    	}

    	if (fileInfoMap[fileInfo.id].state == LibFile.FileState.FILE_INVALID) {
     		errno = uint256(FileError.ID_NOT_EXISTS);
     		log("update file not exists");
     		Notify(errno, "update file not exists");
     		return false;
     	}

     	
     	if (!fileInfo.container.equals("")) {
     		//如果更新container则校验ID
     	}

     	//update info
     	fileInfo.state = LibFile.FileState.FILE_VALID;
        fileInfo.updateTime = now * 1000;
        
     	fileInfoMap[fileInfo.id] = fileInfo;
     	errno = uint256(FileError.NO_ERROR);
     	log("update file success");
     	Notify(errno, "update file success");
     	return true;
    }

    /** find a file by fileId and container */
    function find(string _fileId) constant public returns(string _ret) {
     	_ret = "{\"ret\":0,\"data\":{";

        if (fileInfoMap[_fileId].state != LibFile.FileState.FILE_INVALID) {
            _ret = _ret.concat(fileIdList.length.toKeyValue("total"), ",\"items\":[");
            _ret = _ret.concat(fileInfoMap[_fileId].toJson());
        } else {
            _ret = _ret.concat(fileIdList.length.toKeyValue("total"), ",\"items\":[");
        }

        _ret = _ret.concat("]}}");
    }

    function getCount() constant public returns(uint _ret){
         _ret = fileIdList.length;
    }

    function listAll() constant public returns (string _ret){
        _ret = "{\"ret\":0,\"data\":{";
        _ret = _ret.concat(fileIdList.length.toKeyValue("total"), ",\"items\":[");
        uint total = 0;
        for (uint index = 0; index < fileIdList.length; index++) {
            if (fileInfoMap[fileIdList[index]].state != LibFile.FileState.FILE_INVALID) {
                if (total > 0) {
                    _ret = _ret.concat(",");
                }
                _ret = _ret.concat(fileInfoMap[fileIdList[index]].toJson());

                total = total + 1;
            }
        }
        
        _ret = _ret.concat("]}}");
    }

    /** list all file info by group */
    function listByGroup(string _group) constant public returns (string _ret){
        _ret = "{\"ret\":0,\"data\":{";
        _ret = _ret.concat(fileIdList.length.toKeyValue("total"), ",\"items\":[");
        uint total = 0;
        for (uint index = 0; index < fileIdList.length; index++) {
            if (fileInfoMap[fileIdList[index]].node_group.equals(_group) && fileInfoMap[fileIdList[index]].state != LibFile.FileState.FILE_INVALID) {
                if (total > 0) {
                    _ret = _ret.concat(",");
                }
                _ret = _ret.concat(fileInfoMap[fileIdList[index]].toJson());

                total = total + 1;
            }
        }
        
        _ret = _ret.concat("]}}");
    }

    /** list files info by page in pageSize */
    function pageFiles(uint _pageNo, uint256 _pageSize) constant public returns(string _ret) {
        uint startIndex = uint(_pageNo * _pageSize);
        uint endIndex = uint(startIndex + _pageSize - 1);

        if (startIndex >= fileIdList.length || endIndex <= 0)
        {
            _ret = _ret.concat("{\"ret\":0,\"data\":{\"total\":0,\"items\":[]}}");
            log1("bad parameters in pageFiles", "FileInfoManager");
            return;
        }

        _ret = "{\"ret\":0,\"data\":{";
        _ret = _ret.concat(fileIdList.length.toKeyValue("total"), ",\"items\":[");

        if (endIndex >= fileIdList.length) {
            endIndex = fileIdList.length - 1;
        }

        for (uint index = startIndex; index <= endIndex; index++) {
            if (fileInfoMap[fileIdList[index]].state != LibFile.FileState.FILE_INVALID) {
                _ret = _ret.concat(fileInfoMap[fileIdList[index]].toJson());
                if (index != endIndex){
                    _ret = _ret.concat(",");
                }
            }
        }

        //the {{[ pair
        _ret = _ret.concat("]}}");
    }

    /** get the fixed group in a page, pagesize is default 16 */
    function pageByGroup(string _group, uint256 _pageNo) constant public returns(string _ret) {
        uint total = getGroupFileCount(_group);

        uint pageCount = 0;
        if (total % defaultPageSize == 0)
        {
            pageCount = total / defaultPageSize;
        }
        else {
            pageCount = total / defaultPageSize + 1;
        }

        if (_pageNo < 0 || _pageNo > pageCount-1) {
            log1("bad parameters in pageByGroup", "FileInfoManager");
            _ret = "{\"ret\":0,\"data\":{\"total\":0,[]}}";
            return;
        }

        _ret = "{\"ret\":0,\"data\":{";
        _ret = _ret.concat(total.toKeyValue("total"), ",\"items\":[");

        uint totalBefore = defaultPageSize * _pageNo;
        uint pageStartCount = 0;
        uint startIndex = 0;

        for (uint index = 0; index < fileIdList.length; index++) {
            if (fileInfoMap[fileIdList[index]].state != LibFile.FileState.FILE_INVALID && fileInfoMap[fileIdList[index]].node_group.equals(_group)) {
                pageStartCount = pageStartCount + 1;
            }

            if (pageStartCount >= totalBefore) {
                startIndex = index;
                break;
            }
        }

        uint counter = 0;
        for (index = startIndex; index < fileIdList.length && counter < defaultPageSize; index++) {
            if (fileInfoMap[fileIdList[index]].state != LibFile.FileState.FILE_INVALID && fileInfoMap[fileIdList[index]].node_group.equals(_group)) {
                counter = counter + 1;
                _ret = _ret.concat(fileInfoMap[fileIdList[index]].toJson());
                if (counter < defaultPageSize && index < fileIdList.length-1){
                    _ret = _ret.concat(",");
                }
            }
        }

        //the {{[ pair
        _ret = _ret.concat("]}}");
    }

    /**  get the total pages in a group */
    function getGroupPageCount(string _group) constant public returns(uint256 _ret) {
        uint count = getGroupFileCount(_group);
        if (count % defaultPageSize == 0)
        {
            _ret = count / defaultPageSize;
        }
        else {
            _ret = count / defaultPageSize + 1;
        }
    }

    /**  get the total files in one group  */
    function getGroupFileCount(string _group) constant public returns(uint256 _ret) {
        _ret = 0;
        for (uint index = 0; index < fileIdList.length; index++) {
            if (fileInfoMap[fileIdList[index]].state != LibFile.FileState.FILE_INVALID && fileInfoMap[fileIdList[index]].node_group.equals(_group)) {
                _ret = _ret + 1;
            }
        }
    }

    /** get current page size */
    function getCurrentPageSize() constant public returns(uint256 _ret) {
        _ret = uint256(defaultPageSize);
    }

    /** get default page count */
    function getCurrentPageCount() constant public returns(uint256 _ret) {
        if (fileIdList.length % defaultPageSize == 0)
        {
            _ret = fileIdList.length / defaultPageSize;
        }
        else {
            _ret = fileIdList.length / defaultPageSize + 1;
        }
    }

    /**  create a unqiue file ID by group, server, filename and time */
    function generateFileID(string _salt, string _groupID, string _serverId, string _filename) constant public returns(string _ret){
        //sha3(salt-groupid-serverid-filename-timestamp)
        _ret = _salt;
        _ret = _ret.concat("-").concat(_groupID);
        _ret = _ret.concat("-").concat(_serverId);

        uint milliTime = now * 1000;
        _ret = _ret.concat("-").concat(_filename);
        _ret = _ret.concat("-").concat(milliTime.toString());
        uint sha3Value = uint(sha3(_ret));//bytes32
        _ret = sha3Value.toHexString64().toLower().substr(2, 64);
    }

}
