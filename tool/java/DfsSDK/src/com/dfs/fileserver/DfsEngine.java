/**  
 * @Title: JuzhenDfsClient.java

 * @Description: dfs client

 * @date: 2017年3月2日 下午4:34:38

 * @version: V1.0  

 */
package com.dfs.fileserver;

import java.io.File;
import java.io.IOException;
import java.math.BigInteger;
import java.util.Calendar;
import java.util.Collections;
import java.util.List;
import java.util.Random;
import java.util.Vector;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;

import org.junit.internal.runners.ErrorReportingRunner;
import org.juzix.web3j.NonceTransactionManager;
import org.juzix.web3j.protocol.CostomerWeb3j;
import org.juzix.web3j.protocol.CustomerWeb3jFactory;
import org.web3j.abi.datatypes.Utf8String;
import org.web3j.abi.datatypes.generated.Uint256;
import org.web3j.crypto.Credentials;
import org.web3j.crypto.WalletUtils;
import org.web3j.protocol.Web3jService;
import org.web3j.protocol.core.methods.response.EthAccounts;
import org.web3j.protocol.core.methods.response.TransactionReceipt;
import org.web3j.protocol.http.HttpService;
import org.web3j.protocol.parity.Parity;
import org.web3j.tx.ClientTransactionManager;
import org.web3j.tx.RawTransactionManager;
import org.web3j.tx.TransactionManager;

import com.alibaba.fastjson.JSON;
import com.alibaba.fastjson.JSONObject;
import com.dfs.entity.DfsConst;
import com.dfs.entity.QueryResultJson;
import com.dfs.entity.RetEnum;
import com.dfs.http.FileHttpClient;
import com.dfs.message.FileInfo;
import com.dfs.message.FileServer;
import com.dfs.utils.DfsMD5;
import com.dfs.utils.DfsMutexLock;
import com.dfs.utils.DfsTimer;
import com.dfs.web3j.FileInfoManager;
import com.dfs.web3j.FileServerManager;
import com.dfs.web3j.FileInfoManager.NotifyEventResponse;
import com.dfs.web3j.tx.DfsRawTransactionManager;


public class DfsEngine {
	//request, and then listener callback
	private IDfsListener listener;
	private DfsMutexLock locker;
	protected String		host;
	protected int			port;
	private boolean		isInit = false;
	private String 		walletPassword;
	private String 		keyPath;
	
	private static final String FILE_SERVER_MGR_ADDR = "0x1000000000000000000000000000000000000019";
	private static final String FILE_INFO_MGR_ADDR = "0x1000000000000000000000000000000000000018";
	private String fileServerContract;
	private String fileInfoContract;
	private int use_transaction_type = UserTransactionType.NonSigned;//1 for rawTransation, 2 for weiwan rawTransactio, 3: nonceCustomerizeTransction
	
	private FileHttpClient			httpClient;
	private String					currentAccount;
	private Credentials				credentials;
	private BigInteger				gasPrice = new BigInteger("99999999999");
	private BigInteger				gasLimited = new BigInteger("9999999999999");
	private Vector<FileServer> 		fileServers = new Vector<FileServer>();
	
	private class UserTransactionType {
		public static final int NonSigned = 0;
		public static final int StandartRawSigned = 1;
		public static final int WeiWanRawSigned = 2;
		public static final int NonceCustomerizedRawSigned = 3;
	}
	
	public DfsEngine() {
		fileInfoContract = FILE_INFO_MGR_ADDR;
		fileServerContract = FILE_SERVER_MGR_ADDR;
		credentials = null;
		httpClient = new FileHttpClient();
		locker = new DfsMutexLock();
	}

	/**
	 * @description 初始化, 微万合作版本请使用此接口
	 * @param host 主机地址
	 * @param port JSONRPC端口
	 * @param listener 监听回调接口
	 * @param walletPasswd 加密口令
	 * @param walletFile 钱包文件 
	 * @param fileContract 文件信息合约
	 * @param serverContract 文件服务合约 
	 * @return 0 ：成功    其他：失败
	 */
	public int init(String host, int port, IDfsListener listener, String walletPasswd, String walletFile, String fileContract, String serverContract)
	{
		fileInfoContract = fileContract;
		fileServerContract = serverContract;
		if (walletFile.isEmpty() || walletPasswd.isEmpty())
			return init(host, port, listener);

		locker.lock();/////////
		registerListener(listener);
		
		this.host = host;
		this.port = port;
		httpClient.init(host, port);
		
		walletPassword = walletPasswd;
		keyPath = walletFile;
		if (fileContract.isEmpty() && fileServerContract.isEmpty()) {
			use_transaction_type = UserTransactionType.StandartRawSigned;
		}
		else {
			use_transaction_type = UserTransactionType.WeiWanRawSigned;
		}
		
        try {
        	// 加载证书
        	credentials = WalletUtils.loadCredentials(walletPassword, keyPath);
		} catch (Exception e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
			locker.unlock();
			return DfsConst.DFS_ERROR_BAD;
		}
        listFileServersSync();
        
		isInit = true;
      
        if (fileServers.isEmpty()) {
        	FileServer server = new FileServer();
        	server.setPort(port);
        	server.setHost(host);
        	fileServers.add(server);
        	locker.unlock();/////////
			return DfsConst.DFS_ERROR_OK;
		}
        
		locker.unlock();/////////
		return DfsConst.DFS_ERROR_OK;
	}
	
	/**
	 * @description 初始化, 标准代签名
	 * @param host 主机地址
	 * @param port JSONRPC端口
	 * @param listener 监听回调接口
	 * @param walletPasswd 加密口令
	 * @param walletFile 钱包文件 
	 * @return 0 ：成功    其他：失败
	 */
	public int init(String host, int port, IDfsListener listener, String walletPasswd, String walletFile)
	{
		return init(host, port, listener, walletPasswd, walletFile, false);
	}
	
	/**
	 * @description 初始化, 签名 (可以指定是否使用拓展Nonce)
	 * @param host 主机地址
	 * @param port JSONRPC端口
	 * @param listener 监听回调接口
	 * @param walletPasswd 加密口令
	 * @param walletFile 钱包文件 
	 * @param nonceExtension  是否使用拓展Nonce
	 * @return 0 ：成功    其他：失败
	 */
	public int init(String host, int port, IDfsListener listener, String walletPasswd, String walletFile, Boolean nonceExtension)
	{
		if (nonceExtension)
		{
			use_transaction_type = UserTransactionType.NonceCustomerizedRawSigned;
		}
		else {
			use_transaction_type = UserTransactionType.StandartRawSigned;
		}
		
		locker.lock();/////////
		registerListener(listener);
		
		this.host = host;
		this.port = port;
		httpClient.init(host, port);
		
		walletPassword = walletPasswd;
		keyPath = walletFile;
		
        try {
        	// 加载证书
        	credentials = WalletUtils.loadCredentials(walletPassword, keyPath);
		} catch (Exception e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
			locker.unlock();
			return DfsConst.DFS_ERROR_BAD;
		}
        listFileServersSync();
        
		isInit = true;
      
        if (fileServers.isEmpty()) {
        	FileServer server = new FileServer();
        	server.setPort(port);
        	server.setHost(host);
        	fileServers.add(server);
        	locker.unlock();/////////
			return DfsConst.DFS_ERROR_OK;
		}
        
		locker.unlock();/////////
		return DfsConst.DFS_ERROR_OK;
	}
	
	/**
	 * @description 初始化 （交易不签名， 方式使用SDK）
	 * @param host 主机地址
	 * @param port JSONRPC端口
	 * @param listener 监听回调接口
	 * @return 0 ：成功    其他：失败
	 */
	public int init(String host, int port, IDfsListener listener)
	{
		locker.lock();//////////////
		isInit = false;
		registerListener(listener);
		
		use_transaction_type = UserTransactionType.NonSigned;
		
		this.host = host;
		this.port = port;
		httpClient.init(host, port);
		Parity parity = createParity(host, port);
		
		currentAccount = getFirstInnerAccount(parity);
		listFileServersSync();
		
		isInit = true;
      
		if (fileServers.isEmpty()) {
			FileServer server = new FileServer();
	    	server.setPort(port);
	    	server.setHost(host);
			fileServers.add(server);
	    	locker.unlock();/////////
			return DfsConst.DFS_ERROR_FAIL;
		}
    
		locker.unlock();//////////////
		
		return DfsConst.DFS_ERROR_OK;
	}

	/**
	 * @description 下载文件
	 * @param fileid 文件ID
	 * @param store_path 文件存储完整路径
	 * @return
	 */
	public int downloadFile(String fileid, String storePath)
	{
		if (!isInit || listener == null)
			return DfsConst.DFS_ERROR_BAD;
		
		new Thread() {
            public void run() {
            	FileServer fileServer = getCurrentFileServer();
            	FileInfoManager fileInfoManager = createFileInfoManager(fileServer);
            	
            	//find in contract
            	Vector<FileInfo> fileinfos = new Vector<FileInfo>();
        		StringBuilder info = new StringBuilder();
            	int ret = findFileFromBlock(fileInfoManager, fileid, fileinfos, info);
            	if (ret != DfsConst.DFS_ERROR_OK) {
            		//System.out.println("cannot find the file: " + fileid);
            		listener.errorCallback(ret, info.toString());
            		return;
            	}
            	
            	if (DfsConst.DFS_ERROR_OK != httpClient.download(fileServer, fileid, storePath))
            	{
            		listener.errorCallback(httpClient.getRet(), httpClient.getInfo());
            		return;
            	}
            	
            	//validate the file hash
            	String strMd5 = DfsMD5.getMd5(storePath);
            	if (!strMd5.equals(fileinfos.elementAt(0).getFile_hash())){
            		listener.errorCallback(DfsConst.DFS_ERROR_FAIL, "file corrupted, file imcompleted !");
            		return;
            	}
            	
            	listener.downloadFileCallback(httpClient.getRet(), fileinfos.elementAt(0));
            }
            }.start();
            
        return DfsConst.DFS_ERROR_OK;
	}
	
	/**
	 * @description 上传文件
	 * @param fileInfo 文件信息
	 * @param file 文件路径
	 * @return
	 */
	public int uploadFile(FileInfo fileInfo, String file) {
		FileServer fileServer = getCurrentFileServer();
		if (!isInit || listener == null || !checkFileServer(fileServer))
			return DfsConst.DFS_ERROR_BAD;
		
		File fileObj = new File(file);
		if (!fileObj.exists()) {
			return DfsConst.DFS_ERROR_BAD_PARAMETER;
		}

		//async process
		new Thread() {
            public void run() {
        		String fName = file.trim();  
        		String fileName = fName.substring(fName.lastIndexOf(File.separatorChar)+1); 
        		if (fileName.isEmpty())
        			fileName = "";
        		
        		DfsTimer timer = new DfsTimer();
        		timer.start();
        		String fileid = fetchFileId(fileServer, fileName);
        		timer.stop();
        		
        		timer.start();
        		fileInfo.setId(fileid);
        		fileInfo.setContainer(DfsConst.HTTP_FS_CONTAINER_ID_DEFAULT);
        		fileInfo.setNode_group(fileServer.getGroup());
        		fileInfo.setSrc_node(fileServer.getId());
        		fileInfo.setFilename(fileName);
        		fileInfo.setSize((int)fileObj.length());
        		
        		String fileHash = DfsMD5.getMd5(file);
        		fileInfo.setFile_hash(fileHash);
        		
        		Calendar cal = Calendar.getInstance();
        		fileInfo.setUpdateTime((int)cal.getTimeInMillis()/1000);
            	StringBuffer strRemoteFileHash = new StringBuffer();
            	timer.stop();
            	
            	timer.start();
        		if (DfsConst.DFS_ERROR_OK != httpClient.upload(fileServer, fileInfo, file, strRemoteFileHash))
            	{
            		listener.errorCallback(httpClient.getRet(), httpClient.getInfo());
            		return;
            	}
        		timer.stop();
        		
        		if (strRemoteFileHash.toString().equals(fileHash))
        		{
        			httpClient.deleteFile(fileServer, fileInfo.getId());
                	listener.errorCallback(DfsConst.DFS_ERROR_FAIL, "upload file not complete, try again");
                	return;
        		}
        		
        		FileInfoManager fileInfoManager = createFileInfoManager(fileServer);
            	
            	Utf8String fileStr = new Utf8String(JSON.toJSONString(fileInfo));
            	TransactionReceipt receipt = null;
            	timer.start();
            	try {
            		receipt = fileInfoManager.insert(fileStr).get(DfsConst.JU_REQUEST_TIMEOUT, TimeUnit.SECONDS);
            	} catch (Exception e) {
					// TODO Auto-generated catch block
					e.printStackTrace();
					//System.out.println("upload transaction Exception !");
					httpClient.deleteFile(fileServer, fileInfo.getId());
                	listener.errorCallback(DfsConst.DFS_ERROR_FAIL, "upload transaction Exception");
                	return;
				}
            	timer.stop();
            	
            	timer.start();
            	List<NotifyEventResponse> events = fileInfoManager.getNotifyEvents(receipt);
            	if (events.isEmpty())
                {
                	//System.out.println("upload no event");
                	deleteFile(fileInfo.getId());
                	listener.errorCallback(0, "upload no event");
            		return;
                }
                
                int errno = events.get(0)._errno.getValue().intValue();
                if (errno != 0)
                {
                	//System.out.println("upload transaction failed !");
                	listener.errorCallback(0, "upload transaction failed");
            		return;
                }
                
            	listener.uploadFileCallback(httpClient.getRet(), fileInfo);
            	timer.stop();
            	return;
            }
            }.start();
            
        return DfsConst.DFS_ERROR_OK;
	}
	
	/**
	 * @description 删除文件
	 * @param fileid 文件id
	 * @return
	 */
	public int deleteFile(String fileid)
	{
		FileServer fileServer = getCurrentFileServer();
		if (!isInit || listener == null || !checkFileServer(fileServer))
			return DfsConst.DFS_ERROR_BAD;
		
		new Thread() {
            public void run() {
            	FileInfoManager fileInfoManager = createFileInfoManager(fileServer);
            	
            	FileInfo objFileInfo = new FileInfo();
            	objFileInfo.setId(fileid);
            	
            	if (DfsConst.DFS_ERROR_OK != httpClient.deleteFile(fileServer, fileid)) {
            		//System.out.println("delete file failed, fileid: " + fileid);
            		listener.errorCallback(httpClient.getRet(), httpClient.getInfo());
            		return;
            	}
            	
            	//delete transaction
            	Utf8String fileidStr = new Utf8String(fileid);
            	TransactionReceipt receipt = null;
            	try {
            		receipt = fileInfoManager.deleteById(fileidStr).get(DfsConst.JU_REQUEST_TIMEOUT, TimeUnit.SECONDS);
				} catch (Exception e) {
					// TODO Auto-generated catch block
					e.printStackTrace();
				}
            	
            	List<NotifyEventResponse> events = fileInfoManager.getNotifyEvents(receipt);
            	if (events.size() <= 0)
                {
                	//System.out.println("delete event not match");
                	listener.errorCallback(0, "delete event not match");
            		return;
                }
                
                int errno = events.get(0)._errno.getValue().intValue();
                if (events.get(0)._errno.getValue().intValue() != 0)
                {
                	//System.out.println("delete transaction failed !");
                	listener.errorCallback(errno, events.get(0)._info.getValue());
            		return;
                }
            	
        		listener.deleteFileCallback(httpClient.getRet(), objFileInfo);
        		return;
            }
		}.start();
		
		return DfsConst.DFS_ERROR_OK;
	}
	
	/**
	 * @description 删除文件服务节点
	 * @param _serverid 文件服务节点id
	 * @return
	 */
	public int deleteServer(String serverid) {
		if (!isInit || listener == null)
			return DfsConst.DFS_ERROR_BAD;
		
		if (serverid.isEmpty())
			return DfsConst.DFS_ERROR_BAD_PARAMETER;
		
		new Thread() {
            public void run() {
            	FileServerManager fileServerManager = createFileServerManager(getCurrentFileServer());
            	
        		TransactionReceipt receipt = null;
        		try {
        			receipt = fileServerManager.deleteById(new Utf8String(serverid)).get(DfsConst.JU_REQUEST_TIMEOUT, TimeUnit.SECONDS);
        		} catch (Exception e) {
        			// TODO Auto-generated catch block
        			e.printStackTrace();
        			listener.errorCallback(DfsConst.DFS_ERROR_FAIL, "list fileinfo failed");
        			return;
        		}

        		List<com.dfs.web3j.FileServerManager.NotifyEventResponse> events = fileServerManager.getNotifyEvents(receipt);
        		if (events.isEmpty()){
        			listener.errorCallback(DfsConst.DFS_ERROR_FAIL, "delete server no event back");
        			return;
        		}
        		
        		int errno = events.get(0)._errno.getValue().intValue();
        		if (errno != 0){
        			listener.errorCallback(errno, events.get(0)._info.getValue());
        			return;
        		}
        		
        		FileServer fileServer = new FileServer();
        		fileServer.setId(serverid);
        		listener.deleteServerCallback(errno, fileServer);
        		return;
            }
            }.start();
        
		return DfsConst.DFS_ERROR_OK;
	}
	
	/**
	 * @description 添加一个文件服务节点
	 * @param newServer 添加到文件服务节点
	 * @return 0： 成功， 其他： 失败
	 */
	public int addServer(FileServer newServer) {
		if (!isInit || listener == null)
			return DfsConst.DFS_ERROR_BAD;
		
		if (newServer.getId().isEmpty())
			return DfsConst.DFS_ERROR_BAD_PARAMETER;
		
		new Thread() {
            public void run() {
            	FileServerManager fileServerManager = createFileServerManager(getCurrentFileServer());
            	
            	String serverInfo = JSON.toJSONString(newServer);
        		TransactionReceipt receipt = null;
        		try {
        			Future<TransactionReceipt> futureReceipt = fileServerManager.insert(new Utf8String(serverInfo));
        			receipt = futureReceipt.get(10 * DfsConst.JU_REQUEST_TIMEOUT, TimeUnit.SECONDS);
        		} catch (Exception e) {
        			// TODO Auto-generated catch block
        			e.printStackTrace();
        			listener.errorCallback(DfsConst.DFS_ERROR_FAIL, "add server error");
        			return;
        		}

        		List<com.dfs.web3j.FileServerManager.NotifyEventResponse> events = fileServerManager.getNotifyEvents(receipt);
        		if (events.isEmpty()){
        			listener.errorCallback(DfsConst.DFS_ERROR_FAIL, "add server timeout, no event back");
        			return;
        		}
        		
        		int errno = events.get(0)._errno.getValue().intValue();
        		if (errno != 0){
        			listener.errorCallback(errno, events.get(0)._info.getValue());
        			return;
        		}
        		
        		listener.addServerCallback(errno, newServer);
        		return;
            }
            }.start();
        
		return DfsConst.DFS_ERROR_OK;
	}
	
	/**
	 * @description 添加一个文件服务节点
	 * @param newServer 添加到文件服务节点
	 * @return 0： 成功， 其他： 失败
	 */
	public int addServerSync(FileServer newServer, StringBuffer errorInfo) {
		errorInfo.delete(0, errorInfo.length());
		
		if (!isInit || listener == null) {
			errorInfo.append("not init or null");
			return DfsConst.DFS_ERROR_BAD;
		}
		
		if (newServer.getId().isEmpty()) {
			errorInfo.append("serverid is null");
			return DfsConst.DFS_ERROR_BAD_PARAMETER;
		}
		
		FileServerManager fileServerManager = createFileServerManager(getCurrentFileServer());
            	
    	String serverInfo = JSON.toJSONString(newServer);
		TransactionReceipt receipt = null;
		try {
			Future<TransactionReceipt> futureReceipt = fileServerManager.insert(new Utf8String(serverInfo));
			receipt = futureReceipt.get(10 * DfsConst.JU_REQUEST_TIMEOUT, TimeUnit.SECONDS);
		} catch (Exception e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
			errorInfo.append("add server exception");
			return DfsConst.JU_REQUEST_TIMEOUT;
		}

		List<com.dfs.web3j.FileServerManager.NotifyEventResponse> events = fileServerManager.getNotifyEvents(receipt);
		if (events.isEmpty()){
			errorInfo.append("add server timeout, no event back");
			return DfsConst.DFS_ERROR_FAIL;
		}
		
		int errno = events.get(0)._errno.getValue().intValue();
		if (errno != 0){
			errorInfo.append(events.get(0)._info.getValue());
			return DfsConst.DFS_ERROR_FAIL;
		}
		
		return DfsConst.DFS_ERROR_OK;
	}

	/**
	 * @description 开启或关闭文件服务
	 * @param state 1: 开启， 0： 关闭
	 * @return 0: 成功， 其他：失败
	 */
	public int enableServer(String serverID, boolean enable) {
		if (!isInit || listener == null)
			return DfsConst.DFS_ERROR_BAD;
		
		new Thread() {
            public void run() {
            	FileServerManager fileServerManager = createFileServerManager(getCurrentFileServer());
            	long nEnable = enable ? 1: 0;
        		TransactionReceipt receipt = null;
        		try {
        			Future<TransactionReceipt> futureReceipt = fileServerManager.enable(new Utf8String(serverID), new Uint256(BigInteger.valueOf(nEnable)));
        			receipt = futureReceipt.get(DfsConst.JU_REQUEST_TIMEOUT, TimeUnit.SECONDS);
        		} catch (Exception e) {
        			// TODO Auto-generated catch block
        			e.printStackTrace();
        			listener.errorCallback(DfsConst.DFS_ERROR_FAIL, "enable server error");
        			return;
        		}

        		List<com.dfs.web3j.FileServerManager.NotifyEventResponse> events = fileServerManager.getNotifyEvents(receipt);
        		if (events.isEmpty()){
        			listener.errorCallback(DfsConst.DFS_ERROR_FAIL, "enable server timeout, no event back");
        			return;
        		}
        		
        		int errno = events.get(0)._errno.getValue().intValue();
        		if (errno != 0){
        			listener.errorCallback(errno, events.get(0)._info.getValue());
        			return;
        		}
        		
        		listener.enableServerCallback(errno, serverID);
        		return;
            }
            }.start();
        
		return DfsConst.DFS_ERROR_OK;
	}
	
	
	/**
	 * @description 列出所有文件信息
	 * @return 0: 成功， 其他：失败
	 */
	public int listFiles()
	{
		if (!isInit || listener == null)
			return DfsConst.DFS_ERROR_BAD;
		
		new Thread() {
            public void run() {
            	FileInfoManager fileInfoManager = createFileInfoManager(getCurrentFileServer());
            	
            	Future<Utf8String> result = fileInfoManager.listAll();
        		Utf8String fileinfos = null;
        		
        		try {
        			fileinfos = result.get();
        		} catch (InterruptedException e) {
        			// TODO Auto-generated catch block
        			e.printStackTrace();
        		} catch (ExecutionException e) {
        			// TODO Auto-generated catch block
        			e.printStackTrace();
        		}
        		
        		String strFiles = fileinfos.toString();
        		//System.out.println("**** The file info list json: " + strFiles);
        		
        		//parse fileinfo list
        		QueryResultJson objQueryResult = JSON.parseObject(strFiles, QueryResultJson.class);
        		if (objQueryResult.getRet() != DfsConst.DFS_ERROR_OK && 
        				objQueryResult.getRet() != DfsConst.DFS_ERROR_SUCCESS) {
        			listener.errorCallback(objQueryResult.getRet(), "list fileinfo failed");
        			return;
        		}
        		
        		Vector<FileInfo> vecFileinfos = new Vector<FileInfo>();
        		for (JSONObject item : objQueryResult.getData().getItems())
        		{
        			FileInfo objFileInfo = JSON.toJavaObject((JSONObject)item, FileInfo.class);
        			vecFileinfos.addElement(objFileInfo);
        		}
        		listener.listFileinfoCallback(objQueryResult.getRet(), vecFileinfos);
        		return;
            }
            }.start();
        
		return DfsConst.DFS_ERROR_OK;
	}
	
	/**
	 * @description 列出所有文件信息
	 * @return 0: 成功， 其他：失败
	 */
	public int listFilesByGroup(String _group)
	{
		if (!isInit || listener == null)
			return DfsConst.DFS_ERROR_BAD;
		
		new Thread() {
            public void run() {
            	FileInfoManager fileInfoManager = createFileInfoManager(getCurrentFileServer());
            	
            	Future<Utf8String> result = fileInfoManager.listByGroup(new Utf8String(_group));
        		Utf8String fileinfos = null;
        		
        		try {
        			fileinfos = result.get();
        		} catch (InterruptedException e) {
        			// TODO Auto-generated catch block
        			e.printStackTrace();
        		} catch (ExecutionException e) {
        			// TODO Auto-generated catch block
        			e.printStackTrace();
        		}
        		
        		String strFiles = fileinfos.toString();
        		//System.out.println("**** The file info list json: " + strFiles);
        		
        		//parse fileinfo list
        		QueryResultJson objQueryResult = JSON.parseObject(strFiles, QueryResultJson.class);
        		if (objQueryResult.getRet() != DfsConst.DFS_ERROR_OK && 
        				objQueryResult.getRet() != DfsConst.DFS_ERROR_SUCCESS) {
        			listener.errorCallback(objQueryResult.getRet(), "list fileinfo failed");
        			return;
        		}
        		
        		Vector<FileInfo> vecFileinfos = new Vector<FileInfo>();
        		for (JSONObject item : objQueryResult.getData().getItems())
        		{
        			FileInfo objFileInfo = JSON.toJavaObject((JSONObject)item, FileInfo.class);
        			vecFileinfos.addElement(objFileInfo);
        		}
        		listener.listFileinfoByGroupCallback(objQueryResult.getRet(), vecFileinfos);
        		return;
            }
            }.start();
        
		return DfsConst.DFS_ERROR_OK;
	}
	
	/**
	 * @description 查找文件信息
	 * @param fileid 文件ID
	 * @return 0: 成功， 其他：失败
	 */
	public int findFileinfo(String fileid)
	{
		if (!isInit || listener == null)
			return DfsConst.DFS_ERROR_BAD;
		
		if (fileid == "" ){
			System.out.println("Bad parameters");
			return DfsConst.DFS_ERROR_BAD;
		}
		
		new Thread(){
			public void run() {
				FileInfoManager fileInfoManager = createFileInfoManager(getCurrentFileServer());
				
        		Vector<FileInfo> vecFileinfos = new Vector<FileInfo>();
        		StringBuilder strErr = new StringBuilder();
        		int ret = findFileFromBlock(fileInfoManager, fileid, vecFileinfos, strErr);
        		if (DfsConst.DFS_ERROR_OK != ret) {
        			//System.out.println("cannot find the file: " + fileid);
        			listener.errorCallback(ret, strErr.toString());
        			return;
        		}
        		
        		listener.findFileinfoCallback(ret, vecFileinfos.elementAt(0));
        		return;
			}
		}.start();
		
		return DfsConst.DFS_ERROR_OK;
	}
	
	/**
	 * @description 查询文件信息 （同步方式）
	 * @param fileInfoManager
	 * @param fileid
	 * @param fileinfos
	 * @param info
	 * @return
	 */
	private int findFileFromBlock(FileInfoManager fileInfoManager, String fileid, Vector<FileInfo> fileinfos, StringBuilder info)
	{
		info.delete(0,  info.length());
		
		Future<Utf8String> result = fileInfoManager.find(new Utf8String(fileid));
		Utf8String fileInfo = null;
		try {
			fileInfo = result.get();
		} catch (Exception e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
			
			info.append("god exception: " + e.getMessage());
			return DfsConst.DFS_ERROR_NETWORK_EXCEPTION;
		}
		
		//System.out.println("find response json: " + fileInfo.getValue());
		
		
		//parse the FileInfo object
		String strFileInfo = fileInfo.getValue().toString();
		//System.out.println("**** The file info json: " + strFileInfo);
		
		//parse fileinfo list
		QueryResultJson objQueryResult = JSON.parseObject(strFileInfo, QueryResultJson.class);
		if (objQueryResult.getRet() != RetEnum.RET_SUCCESS.getCode() ||
				objQueryResult.getData().getItems().length < 1) {
			info.append("find fileinfo failed");
			return DfsConst.DFS_ERROR_FILE_NOT_FIND;
		}
		
		for (JSONObject item : objQueryResult.getData().getItems())
		{
			FileInfo objFileInfo = JSON.toJavaObject((JSONObject)item, FileInfo.class);
			fileinfos.addElement(objFileInfo);
		}
		
		return DfsConst.DFS_ERROR_OK;
	}
	
	/**
	 * @description 获取cache的服务节点列表
	 * @return 节点列表
	 */
	public Vector<FileServer> getFileServers() {
		return fileServers;
	}
	
	/**
	 * @description 获取文件服务节点列表 （节点信息返回后将会刷新缓存）
	 * @return
	 */
	public int listServers() {
		if (!isInit || listener == null)
			return DfsConst.DFS_ERROR_BAD;
		
		new Thread(){
			public void run() {
				FileServerManager fileServerManager = createFileServerManager(getCurrentFileServer());
				
				Future<Utf8String> futureStr = fileServerManager.listAll();
				Utf8String strUtf8Json = null;
				try {
					strUtf8Json = futureStr.get();
				} catch (Exception e) {
					// TODO Auto-generated catch block
					e.printStackTrace();
					listener.errorCallback(DfsConst.DFS_ERROR_FAIL, "list server excetion");
					return;
				}
				
				String strjson = strUtf8Json.getValue();
				QueryResultJson objQueryJson = JSON.parseObject(strjson, QueryResultJson.class);
				System.out.println("json: " + strjson);
				if (objQueryJson.getRet() != DfsConst.DFS_ERROR_OK && 
						objQueryJson.getRet() != DfsConst.DFS_ERROR_SUCCESS) {
					listener.errorCallback(objQueryJson.getRet(), "list server failed");
					return;
				}
				
				Vector<FileServer> vecServers = new Vector<FileServer>();
				for (JSONObject object : objQueryJson.getData().getItems()) {
					FileServer server = JSON.toJavaObject(object, FileServer.class);
					vecServers.add(server);
				}
	
				listener.listServersCallback(objQueryJson.getRet(), vecServers);
				return;
			}}.start();
		
			return DfsConst.DFS_ERROR_OK;
	}
	
	/**
	 * @description 按照组ID获取文件服务节点列表 （节点信息返回后将会刷新缓存）
	 * @param group 组ID
	 * @return
	 */
	public int listServerByGroup(String group) {
		if (!isInit || listener == null)
			return DfsConst.DFS_ERROR_BAD;
		
		new Thread(){
			public void run() {
				FileServerManager fileServerManager = createFileServerManager(getCurrentFileServer());
				
				Utf8String utf8String = new Utf8String(group);
				Future<Utf8String> futureStr = fileServerManager.listByGroup(utf8String);
				Utf8String strUtf8Json = null;
				try {
					TimeUnit timeUnit = TimeUnit.SECONDS;
					strUtf8Json = futureStr.get(DfsConst.JU_REQUEST_TIMEOUT/2, timeUnit);
				} catch (Exception e) {
					listener.errorCallback(DfsConst.DFS_ERROR_BAD, "list server by group exception");
					return;
				}
				
				String strjson = strUtf8Json.getValue();
				QueryResultJson objQueryJson = JSON.parseObject(strjson, QueryResultJson.class);
				if (objQueryJson.getRet() != DfsConst.DFS_ERROR_OK &&
						objQueryJson.getRet() != DfsConst.DFS_ERROR_SUCCESS) {
					listener.errorCallback(objQueryJson.getRet(), "list server by group failed");
					return;
				}
				
				Vector<FileServer> vecServers = new Vector<FileServer>();
				for (JSONObject object : objQueryJson.getData().getItems()) {
					FileServer server = JSON.toJavaObject(object, FileServer.class);
					vecServers.add(server);
				}
			
				listener.listServersByGroupCallback(objQueryJson.getRet(), vecServers);
				return;
			}}.start();
		
		return DfsConst.DFS_ERROR_OK;
	}

	/**
	 * 
	 * @description 获取文件ID (同步接口)
	 * @param fileServer 服务节点
	 * @param filename 文件名
	 */
	private String fetchFileId(FileServer fileServer, String filename) {
		FileInfoManager fileInfoManager = createFileInfoManager(fileServer);
		Random random = new Random();
		Calendar cal = Calendar.getInstance();
		String randomString = "" + cal.getTimeInMillis() + random.nextInt(1000000000);
		Future<Utf8String> futureStr = fileInfoManager.generateFileID(new Utf8String(randomString), new Utf8String(fileServer.getGroup()), new Utf8String(fileServer.getId()), new Utf8String(filename));
		Utf8String strFileId = null;
		try {
			strFileId = futureStr.get();
		} catch (Exception e) {
			e.printStackTrace();
			return "";
		}
		
		return strFileId.getValue();
	}
	
	
	/**
	 * @description 同步接口： 获取服务器列表
	 */
	private int listFileServersSync() {
		FileServer fileServer = new FileServer();
		fileServer.setHost(host);
		fileServer.setPort(port);
		
		FileServerManager fileServerManager = createFileServerManager(fileServer);
		Future<Utf8String> futureStr = fileServerManager.listAll();
		Utf8String strUtf8Json = null;
		try {
			strUtf8Json = futureStr.get();
		} catch (InterruptedException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		} catch (ExecutionException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
			return DfsConst.DFS_ERROR_FAIL;
		}
		
		String strjson = strUtf8Json.getValue();
		QueryResultJson objQueryJson = JSON.parseObject(strjson, QueryResultJson.class);
		if (objQueryJson.getRet() != DfsConst.DFS_ERROR_OK && 
				objQueryJson.getRet() != DfsConst.DFS_ERROR_SUCCESS) {
			return DfsConst.DFS_ERROR_FAIL;
		}
		
		Vector<FileServer> vecServers = new Vector<FileServer>();
		for (JSONObject object : objQueryJson.getData().getItems()) {
			FileServer server = JSON.toJavaObject(object, FileServer.class);
			vecServers.add(server);
		}
		if (vecServers.isEmpty())
		{
			return DfsConst.DFS_ERROR_FAIL;
		}
		
		//shuffle the servers
		shuffleServers(vecServers);
		
		locker.lock();
		fileServers.clear();
		for (FileServer server : vecServers) {
			if (server.getEnable() != 0) {
				//we only want the on service server node
				fileServers.addElement(server);
			}
		}
		
		locker.unlock();
		return DfsConst.DFS_ERROR_OK;
	}

	/**
	 * @return the gasPrice
	 */
	public BigInteger getGasPrice() {
		return gasPrice;
	}

	/**
	 * @return the gasLimited
	 */
	public BigInteger getGasLimited() {
		return gasLimited;
	}

	/**
	 * @param keyPath the keyPath to set
	 */
	public void setKeyPath(String keyPath) {
		this.keyPath = keyPath;
	}

	/**
	 * @param 设置gasPrice
	 */
	public void setGasPrice(BigInteger gasPrice) {
		this.gasPrice = gasPrice;
	}

	/**
	 * @param 设置gasLimited
	 */
	public void setGasLimited(BigInteger gasLimited) {
		this.gasLimited = gasLimited;
	}

	/**
	 * @return 获取签名认证对象
	 */
	public Credentials getCredentials() {
		return credentials;
	}

	/**
	 * @description 随机化服务器
	 * @param vecServers 服务器列表  
	 */
	protected void shuffleServers(Vector<FileServer> vecServers) {
		Random random = new Random();
		Calendar calendar = Calendar.getInstance();
		random.setSeed(calendar.getTimeInMillis()/1000);
		Collections.shuffle(vecServers, random);
	}
	
	/**
	 * @param host 主机地址
	 * @param port JSONRPC端口
	 * @return
	 */
	protected Parity createParity(String host, int port) {
		Parity parity;
		String uri = "http://";
		uri += host;
		uri += ":";
		uri += port;
		
		parity = Parity.build(new HttpService(uri));
		return parity;
	}
	
	/**
	 * @param fileServer文件服务器节点
	 * @return
	 */
	protected Parity createParity(FileServer fileServer) {
		return createParity(fileServer.getHost(), fileServer.getPort());
	}
	
	/**
	 * @description 注册监听器
	 * @param listener
	 * @return
	 */
	private int registerListener(IDfsListener listener)
	{
		this.listener = listener;
		return 0;
	}
	
	/**
	 * @description 检查server参数
	 */
	private boolean checkFileServer(FileServer fileServer) {
		if (fileServer.getHost().isEmpty() || fileServer.getPort() <= 0 || !isServerParamValid(fileServer)) {
			return false;
		}
		
		return true;
	}
	
	/**
	 * @description 检查null或空串
	 */
	private boolean isServerParamValid(FileServer fileServer) {
		if (fileServer.getId() == null || fileServer.getGroup() == null) {
			return false;
		}
		
		if (fileServer.getId().isEmpty() || fileServer.getGroup().isEmpty()) {
			return false;
		}
		
		return true;
	}
	
	/**
	 * @param parity
	 */
	private String getFirstInnerAccount(Parity parity) {
		EthAccounts accounts = null;
		try {
			accounts = parity.ethAccounts().send();
		} catch (IOException e) {
			e.printStackTrace();
			return null;
		}
			
		return accounts.getAccounts().get(0);
	}
	
	/**
	 * @description 创建文件信息合约对象
	 */
	private FileInfoManager createFileInfoManager(FileServer fileServer) {
		Parity parity = createParity(fileServer);
		String addr = fileInfoContract;

		if (credentials == null) {
			currentAccount = getFirstInnerAccount(parity);
			return FileInfoManager.load(addr, parity, new ClientTransactionManager(parity, currentAccount), gasPrice, gasLimited);
		}
		
		// with signed, 1 for standard, 2 for weiwan, 3, for nonceExtension
		if (use_transaction_type == UserTransactionType.StandartRawSigned) {
			return FileInfoManager.load(addr, parity, new RawTransactionManager(parity, credentials, 100, 100) , gasPrice, gasLimited);
		}
		else if (use_transaction_type == UserTransactionType.WeiWanRawSigned) {
			return FileInfoManager.load(addr, parity, new DfsRawTransactionManager(parity, credentials, 100, 100) , gasPrice, gasLimited);
		}
		else//3 or other is default for nonceExtension
		{
			String uri = "http://";
			uri += host;
			uri += ":";
			uri += port;
			
			HttpService httpService = new HttpService(uri);
			CostomerWeb3j web3jExt = CustomerWeb3jFactory.buildweb3j(httpService);
			return FileInfoManager.load(addr, parity, new NonceTransactionManager(web3jExt, credentials, 100, 100) , gasPrice, gasLimited);
		}
	}
	
	/**
	 * @description 更新文件服务合约对象
	 */
	private FileServerManager createFileServerManager(FileServer fileServer) {
		Parity parity = createParity(fileServer);
		String addr = fileServerContract;
		
		if (credentials == null) {
			currentAccount = getFirstInnerAccount(parity);
			return FileServerManager.load(addr, parity, new ClientTransactionManager(parity, currentAccount), gasPrice, gasLimited);
		}

		// with signed, 1 for standard, 2 for weiwan, 3, for nonceExtension
		if (use_transaction_type == UserTransactionType.StandartRawSigned) {
			return FileServerManager.load(addr, parity, new RawTransactionManager(parity, credentials, 100, 100) , gasPrice, gasLimited);
		}
		else if (use_transaction_type == UserTransactionType.WeiWanRawSigned) {
			return FileServerManager.load(addr, parity, new DfsRawTransactionManager(parity, credentials, 100, 100) , gasPrice, gasLimited);
		}
		else//3 or other is default for nonceExtension
		{
			String uri = "http://";
			uri += host;
			uri += ":";
			uri += port;
			
			HttpService httpService = new HttpService(uri);
			CostomerWeb3j web3jExt = CustomerWeb3jFactory.buildweb3j(httpService);
			return FileServerManager.load(addr, parity, new NonceTransactionManager(web3jExt, credentials, 100, 100) , gasPrice, gasLimited);
		}
	}

	/**
	 * @return 获取当前文件服务节点
	 */
	public FileServer getCurrentFileServer() {
		for (FileServer server : fileServers) {
			if (server.getEnable() != 0 && server.getHost().equals(host) && server.getPort() == port)
				return server;
		}
		
		FileServer defaultServer = new FileServer();
		defaultServer.setPort(port);
		defaultServer.setHost(host);
		return defaultServer;
	}
}
