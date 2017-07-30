/**
 * 
 */
package com.dfs.web3j.tx;

import java.math.BigInteger;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.List;
import java.util.Random;

import org.web3j.crypto.Credentials;
import org.web3j.crypto.Sign;
import org.web3j.crypto.Sign.SignatureData;
import org.web3j.protocol.core.methods.request.RawTransaction;
import org.web3j.rlp.RlpEncoder;
import org.web3j.rlp.RlpList;
import org.web3j.rlp.RlpString;
import org.web3j.rlp.RlpType;
import org.web3j.utils.Numeric;

/**
 * @author Administrator
 *
 */
public class DfsTransactionEncoder {
	
	private BigInteger blockLimited = new BigInteger("100");
	
	/**
	 * 
	 */
	public DfsTransactionEncoder() {
	}

	public List<RlpType> asRlpValues(
            RawTransaction rawTransaction, SignatureData signatureData) {
        List<RlpType> result = new ArrayList<>();

        Random random = new Random();
        random.setSeed(Calendar.getInstance().getTimeInMillis());
        //rlp[0]  randomid
        BigInteger ramdomid = new BigInteger(256, random);
        result.add(RlpString.create(ramdomid));
        
        result.add(RlpString.create(rawTransaction.getGasPrice()));
        //result.add(RlpString.create(rawTransaction.getNonce()));
        result.add(RlpString.create(rawTransaction.getGasLimit()));
        
        //rlp[3]  blockLimited
        result.add(RlpString.create(blockLimited));

        // an empty to address (contract creation) should not be encoded as a numeric 0 value
        String to = rawTransaction.getTo();
        if (to != null && to.length() > 0) {
            result.add(RlpString.create(Numeric.toBigInt(to)));
        } else {
            result.add(RlpString.create(""));
        }

        result.add(RlpString.create(rawTransaction.getValue()));

        // value field will already be hex encoded, so we need to convert into binary first
        byte[] data = Numeric.hexStringToByteArray(rawTransaction.getData());
        result.add(RlpString.create(data));

        if (signatureData != null) {
            result.add(RlpString.create(signatureData.getV()));
            result.add(RlpString.create(signatureData.getR()));
            result.add(RlpString.create(signatureData.getS()));
        }
        
        return result;
    }
	
	/**
	 * @return the blockLimited
	 */
	public BigInteger getBlockLimited() {
		return blockLimited;
	}

	/**
	 * @param blockLimited the blockLimited to set
	 */
	public void setBlockLimited(BigInteger blockLimited) {
		this.blockLimited = blockLimited;
	}
	
	public byte[] signMessage(RawTransaction rawTransaction, Credentials credentials) {
        byte[] encodedTransaction = encode(rawTransaction);
        org.web3j.crypto.Sign.SignatureData signatureData = Sign.signMessage(
                encodedTransaction, credentials.getEcKeyPair());

        return encode(rawTransaction, signatureData);
    }

    public byte[] signMessage(
            RawTransaction rawTransaction, byte chainId, Credentials credentials) {
        byte[] encodedTransaction = encode(rawTransaction, chainId);
        Sign.SignatureData signatureData = Sign.signMessage(
                encodedTransaction, credentials.getEcKeyPair());

        Sign.SignatureData eip155SignatureData = createEip155SignatureData(signatureData, chainId);
        return encode(rawTransaction, eip155SignatureData);
    }

    public Sign.SignatureData createEip155SignatureData(
    		Sign.SignatureData signatureData, byte chainId) {
        byte v = (byte) (signatureData.getV() + (chainId << 1) + 8);

        return new Sign.SignatureData(
                v, signatureData.getR(), signatureData.getS());
    }

    public byte[] encode(RawTransaction rawTransaction) {
        return encode(rawTransaction, null);
    }

    public byte[] encode(RawTransaction rawTransaction, byte chainId) {
    	Sign.SignatureData signatureData = new Sign.SignatureData(
                chainId, new byte[] {}, new byte[] {});
        return encode(rawTransaction, signatureData);
    }

    private byte[] encode(RawTransaction rawTransaction, Sign.SignatureData signatureData) {
        List<RlpType> values = asRlpValues(rawTransaction, signatureData);
        RlpList rlpList = new RlpList(values);
        return RlpEncoder.encode(rlpList);
    }
}
