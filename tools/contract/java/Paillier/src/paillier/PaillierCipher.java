package paillier;

import java.math.*;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.interfaces.RSAPrivateCrtKey;
import java.security.interfaces.RSAPublicKey;
import java.util.*;

public class PaillierCipher {

	public static String encrypt(BigInteger m, PublicKey publicKey) {
		return CommonUtils.byteToHexString(encryptAsBytes(m, publicKey));
	}
	
	public static byte[] encryptAsBytes(BigInteger m, PublicKey publicKey) {
		RSAPublicKey rsaPubKey = (RSAPublicKey)publicKey;
		BigInteger n = rsaPubKey.getModulus();
		BigInteger g = n.add(BigInteger.ONE);
		
		BigInteger random;
		do{
			random = new BigInteger(n.bitLength(), new Random());
		}while(random.signum() != 1);
		
		if(m.signum() == -1) {
			m = m.mod(n);
		}
		
		BigInteger nsquare = n.multiply(n);
		BigInteger ciphertext = g.modPow(m, nsquare).multiply(random.modPow(n, nsquare)).mod(nsquare);
		
		byte[] nBytes = CommonUtils.asUnsignedByteArray(n);
		byte[] nLenBytes = CommonUtils.unsignedShortToByte2(nBytes.length);
		byte[] cipherBytes = CommonUtils.asUnsignedByteArray(ciphertext, n.bitLength()/4);
		byte[] data = new byte[nLenBytes.length + nBytes.length + cipherBytes.length];
        System.arraycopy(nLenBytes, 0, data, 0, nLenBytes.length);
        System.arraycopy(nBytes, 0, data, nLenBytes.length, nBytes.length);
        System.arraycopy(cipherBytes, 0, data, nLenBytes.length + nBytes.length, cipherBytes.length); 
        return data;
	}

	public static BigInteger decrypt(String ciphertext, PrivateKey privateKey) {
		return decrypt(CommonUtils.hexStringToBytes(ciphertext), privateKey);
	}
	
	public static BigInteger decrypt(byte[] ciphertext, PrivateKey privateKey) {
		RSAPrivateCrtKey rsaPriKey = (RSAPrivateCrtKey)privateKey;
		BigInteger n = rsaPriKey.getModulus();
		BigInteger lambda = rsaPriKey.getPrimeP().subtract(BigInteger.ONE).multiply(rsaPriKey.getPrimeQ().subtract(BigInteger.ONE));

		int nLen = CommonUtils.byte2ToUnsignedShort(ciphertext);
		byte[] nBytes = new byte[nLen];
		System.arraycopy(ciphertext, 2, nBytes, 0, nLen);
		BigInteger n1 = CommonUtils.fromUnsignedByteArray(nBytes);
		if(n1.compareTo(n) != 0) {
			System.err.println("Invalid ciphertext, cannot match n parameter");
			return null;
		}
		
		byte[] data = new byte[ciphertext.length - nLen - 2];
		System.arraycopy(ciphertext, 2+nLen, data, 0, ciphertext.length - nLen - 2);
		BigInteger intCiphertext = CommonUtils.fromUnsignedByteArray(data);
		
		BigInteger mu = lambda.modInverse(n);
		BigInteger nsquare = n.multiply(n);
		BigInteger message = intCiphertext.modPow(lambda, nsquare).subtract(BigInteger.ONE).divide(n).multiply(mu).mod(n);
		BigInteger maxValue = BigInteger.ONE.shiftLeft(n.bitLength() / 2);
		if(message.compareTo(maxValue) == 1) {
			return message.subtract(n);
		}
		else {
			return message;
		}
	}

	public static String ciphertextAdd(String ciphertext1, String ciphertext2) {
		return CommonUtils.byteToHexString(ciphertextAdd(CommonUtils.hexStringToBytes(ciphertext1), CommonUtils.hexStringToBytes(ciphertext2)));
	}
	
	public static byte[] ciphertextAdd(byte[] ciphertext1, byte[] ciphertext2) {
		int nLen1 = CommonUtils.byte2ToUnsignedShort(ciphertext1);
		byte[] nBytes1 = new byte[nLen1];
		System.arraycopy(ciphertext1, 2, nBytes1, 0, nLen1);
		BigInteger n1 = CommonUtils.fromUnsignedByteArray(nBytes1);
		byte[] data1 = new byte[ciphertext1.length - nLen1 - 2];
		System.arraycopy(ciphertext1, 2+nLen1, data1, 0, ciphertext1.length - nLen1 - 2);

		int nLen2 = CommonUtils.byte2ToUnsignedShort(ciphertext2);
		byte[] nBytes2 = new byte[nLen2];
		System.arraycopy(ciphertext2, 2, nBytes2, 0, nLen2);
		BigInteger n2 = CommonUtils.fromUnsignedByteArray(nBytes2);
		if(n2.compareTo(n1) != 0) {
			System.err.println("ciphertext1 cannot match ciphertext2");
			return null;
		}
		
		byte[] data2 = new byte[ciphertext2.length - nLen2 - 2];
		System.arraycopy(ciphertext2, 2+nLen2, data2, 0, ciphertext2.length - nLen2 - 2);
		
		BigInteger ct1 = CommonUtils.fromUnsignedByteArray(data1);
		BigInteger ct2 = CommonUtils.fromUnsignedByteArray(data2);
		BigInteger nsquare = n1.multiply(n1);
		BigInteger ct = ct1.multiply(ct2).mod(nsquare);
		
		byte[] nLenBytes = CommonUtils.unsignedShortToByte2(nBytes1.length);
		byte[] cipherBytes = CommonUtils.asUnsignedByteArray(ct, n1.bitLength()/4);
		byte[] data = new byte[nLenBytes.length + nBytes1.length + cipherBytes.length];  
        System.arraycopy(nLenBytes, 0, data, 0, nLenBytes.length);  
        System.arraycopy(nBytes1, 0, data, nLenBytes.length, nBytes1.length);
        System.arraycopy(cipherBytes, 0, data, nLenBytes.length + nBytes1.length, cipherBytes.length); 
        return data;
	}
}
