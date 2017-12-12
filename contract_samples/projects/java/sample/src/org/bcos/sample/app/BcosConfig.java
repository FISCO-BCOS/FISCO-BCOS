/**
 * @file: BcosConfig.java
 * @author: fisco-dev
 * 
 * @date: 2017
 */

package org.bcos.sample.app;

public class BcosConfig {
	private String rpc_host;
	private int rpc_port;
	private String contract_binary;
	private String contract_abi;
	private String wallet;
	private String password;
	
	public BcosConfig() {
	}
	
	/**
	 * @return the rpc_host
	 */
	public String getRpc_host() {
		return rpc_host;
	}
	/**
	 * @param rpc_host the rpc_host to set
	 */
	public void setRpc_host(String rpc_host) {
		this.rpc_host = rpc_host;
	}
	/**
	 * @return the rpc_port
	 */
	public int getRpc_port() {
		return rpc_port;
	}
	/**
	 * @param rpc_port the rpc_port to set
	 */
	public void setRpc_port(int rpc_port) {
		this.rpc_port = rpc_port;
	}
	/**
	 * @return the contract_binary
	 */
	public String getContract_binary() {
		return contract_binary;
	}
	/**
	 * @param contract_binary the contract_binary to set
	 */
	public void setContract_binary(String contract_binary) {
		this.contract_binary = contract_binary;
	}
	/**
	 * @return the contract_abi
	 */
	public String getContract_abi() {
		return contract_abi;
	}
	/**
	 * @param contract_abi the contract_abi to set
	 */
	public void setContract_abi(String contract_abi) {
		this.contract_abi = contract_abi;
	}
	/**
	 * @return the wallet
	 */
	public String getWallet() {
		return wallet;
	}
	/**
	 * @param wallet the wallet to set
	 */
	public void setWallet(String wallet) {
		this.wallet = wallet;
	}
	/**
	 * @return the password
	 */
	public String getPassword() {
		return password;
	}
	/**
	 * @param password the password to set
	 */
	public void setPassword(String password) {
		this.password = password;
	}
}
