package org.bcos.sample.app;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileReader;
import java.io.IOException;
import java.math.BigInteger;
import java.net.URL;

import org.bcos.sample.web3j.SimpleStorage;
import org.juzix.web3j.protocol.CostomerWeb3j;
import org.juzix.web3j.protocol.CustomerWeb3jFactory;
import org.web3j.abi.datatypes.Address;
import org.web3j.crypto.Credentials;
import org.web3j.crypto.WalletUtils;
import org.web3j.protocol.core.methods.response.TransactionReceipt;
import org.web3j.protocol.http.HttpService;
import org.web3j.protocol.parity.Parity;

import com.alibaba.fastjson.JSON;

import sun.awt.image.IntegerInterleavedRaster;

public class Main {

	public static void main(String[] args) {
		BcosApp app = new BcosApp();
		
		//1. load config of the sample application
		BcosConfig configure = app.loadConfig();
		if (configure == null) {
			System.err.println("error in load configure, init failed  !!!");
			return;
		}
		System.out.println("load configure successully !");
		
		//2. deploy the contract to BCOS blockchain
		Address address = app.deployContract();
		if (address == null) {
			System.err.println("error in deploy contract !!!");
			return;
		}
		System.out.println("deploy SimpleStorage success, address: " + address.toString());
		
		//3. send Raw Transaction to blockchain
		TransactionReceipt receipt = app.executeTransaction(address);
		if (receipt == null) {
			System.err.println("error in executeTransaction  !!!");
			return;
		}
		System.out.println("execute SimpleStorage transaction success, TxHash: " + receipt.getTransactionHash());
		
		//4. send call jsonrpc to blockchain
		BigInteger value = app.executeCall(address);
		System.out.println("the call value: " + value.intValue());
	}

}
