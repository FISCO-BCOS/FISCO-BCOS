/**
 * @file: BcosApp.java
 * @author: fisco-dev
 * 
 * @date: 2017
 */
 
package org.bcos.sample.app;

import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.math.BigInteger;
import java.net.URL;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Future;

import org.bcos.sample.web3j.SimpleStorage;
import org.bcos.web3j.tx.BcosRawTxManager;
import org.web3j.abi.datatypes.Address;
import org.web3j.abi.datatypes.generated.Uint256;
import org.web3j.crypto.Credentials;
import org.web3j.crypto.WalletUtils;
import org.web3j.protocol.core.methods.response.TransactionReceipt;
import org.web3j.protocol.http.HttpService;
import org.web3j.protocol.parity.Parity;

import com.alibaba.fastjson.JSON;

public class BcosApp {
	private BcosConfig configure;
	private Address deployAddress;
	private SimpleStorage simpleStorage;
	private Parity web3j;
	private Credentials credentials;
	
	public static BigInteger gasPrice = new BigInteger("99999999999");
	public static BigInteger gasLimited = new BigInteger("9999999999999");
	public static BigInteger initialValue = new BigInteger("0");
	
	public BcosApp() {
		configure = null;
		deployAddress = null;
		simpleStorage = null;
		credentials = null;
		web3j = null;
	}
	
	public BcosConfig loadConfig(){
		URL path = Thread.currentThread().getContextClassLoader().getResource("");
		String strFile = (path.getPath() + "../res/config.json");
		System.out.println("the path: " + strFile);
		
		File file = new File(strFile);
		if (!file.exists()) {
			System.err.println("*** config file not exists ***");
			return null;
		}
		
		FileReader fReader = null;
		
		char cbuf[] = new char[4096];
		StringBuffer configString = new StringBuffer();
		try {
			fReader = new FileReader(file);
			int fileLen = (int)file.length();
			int bytes = 0;
			bytes = fReader.read(cbuf, 0, 4096);
			if (bytes == fileLen) {
				configString.append(cbuf, 0, bytes);
			}
			else {
				while(bytes < fileLen) {
					int nRead = fReader.read(cbuf, 0, 4096);
					if (nRead > 0) {
						configString.append(cbuf, 0, nRead);
						bytes += nRead;
					}
					else if (nRead == -1) {
						break;
					}
					else {
						System.out.println("*** error in reading config file ***");
					}
				}
			}
		} catch (IOException e) {
			e.printStackTrace();
		} finally {
			try {
				fReader.close();
			} catch (IOException e) {
				e.printStackTrace();
				return null;
			}
		}
		
		System.out.println("the config file: \n" + configString.toString());
		
		BcosConfig config = JSON.parseObject(configString.toString(), BcosConfig.class);
		System.out.println("the config: " + config.toString());
		if (!config.getWallet().isEmpty() && config.getWallet().charAt(0) != '/') {
			config.setWallet(path.getPath() + "../res/" + config.getWallet());
		}
		configure = config;
		
		System.out.println("the wallet: " + config.getWallet());
		try {
        	// 加载证书
			credentials = WalletUtils.loadCredentials(configure.getPassword(), new File(configure.getWallet()));
			System.out.println("address: " + credentials.getAddress());
			
		} catch (Exception e) {
			e.printStackTrace();
			System.err.println("*** load wallet failed  ****");
			return null;
		}
		
		// service url 
		String uri = "http://";
		uri += configure.getRpc_host();
		uri += ":";
		uri += configure.getRpc_port();
		
		HttpService httpService = new HttpService(uri);
		web3j = Parity.build(httpService);
		return config;
	}
	
	public Address deployContract() {
		if (configure == null || web3j == null || credentials == null)
			return null;
		
		Future<SimpleStorage> futureSimpleStorage = SimpleStorage.deploy(web3j, new BcosRawTxManager(web3j, credentials, 100, 100), gasPrice, gasLimited, new BigInteger("0"));
	
		try {
			simpleStorage = futureSimpleStorage.get();
		} catch (InterruptedException | ExecutionException e) {
			e.printStackTrace();
			System.err.println("*** deploy contract failed *** ");
			return null;
		}
		
		//System.out.println("deploy success, address: " + simpleStorage.getContractAddress());
		return new Address(simpleStorage.getContractAddress());
	}
	
	public TransactionReceipt executeTransaction(Address address) {
		if (configure == null || web3j == null || credentials == null)
			return null;
		
		if (address != null) {
			simpleStorage = SimpleStorage.load(address.toString(), web3j, new BcosRawTxManager(web3j, credentials, 100, 100), gasPrice, gasLimited);
		}
		
		TransactionReceipt receipt = null;
		try {
			receipt = simpleStorage.set(new Uint256(new BigInteger("1000"))).get();
		} catch (InterruptedException | ExecutionException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
		
		//System.out.println("execute transaction successully, txHash: " + receipt.getTransactionHash());
		return receipt;
	}
	
	public BigInteger executeCall(Address address) {
		if (configure == null || web3j == null || credentials == null)
			return null;
		
		if (address != null) {
			simpleStorage = SimpleStorage.load(address.toString(), web3j, new BcosRawTxManager(web3j, credentials, 100, 100), gasPrice, gasLimited);
		}
		
		BigInteger storedData =  null;
		try {
			Uint256 value = simpleStorage.get().get();
			storedData = value.getValue();
		} catch (InterruptedException | ExecutionException e) {
			e.printStackTrace();
		}
		
		//System.out.println("the call value: " + storedData.intValue());
		return storedData;
	}

	/**
	 * @return the configure
	 */
	public BcosConfig getConfigure() {
		return configure;
	}

	/**
	 * @param configure the configure to set
	 */
	public void setConfigure(BcosConfig configure) {
		this.configure = configure;
	}

	/**
	 * @return the deployAddress
	 */
	public Address getDeployAddress() {
		return deployAddress;
	}

	/**
	 * @param deployAddress the deployAddress to set
	 */
	public void setDeployAddress(Address deployAddress) {
		this.deployAddress = deployAddress;
	}
}
