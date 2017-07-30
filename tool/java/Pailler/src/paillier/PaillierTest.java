package paillier;

import java.math.BigInteger;
import java.security.KeyPair;
import java.security.interfaces.RSAPrivateCrtKey;
import java.security.interfaces.RSAPrivateKey;
import java.security.interfaces.RSAPublicKey;

public class PaillierTest {
	public static void test1() {
		KeyPair keypair = PaillierKeyPair.generateKeyPair(1024);
		RSAPublicKey pubKey = (RSAPublicKey)keypair.getPublic();
		RSAPrivateCrtKey priKey = (RSAPrivateCrtKey)keypair.getPrivate();
		System.out.println("e:" + priKey.getPublicExponent().intValue());
		
		String publicKeyStr = PaillierKeyPair.publicKeyToPem(pubKey);
		String privateKeyStr = PaillierKeyPair.privateKeyToPem(priKey);

		System.out.println("public key:" + publicKeyStr);
		System.out.println("private key:" + privateKeyStr);
		
		RSAPublicKey pubKey1 = (RSAPublicKey)PaillierKeyPair.pemToPublicKey(publicKeyStr);
		RSAPrivateKey priKey1 = (RSAPrivateKey)PaillierKeyPair.pemToPrivateKey(privateKeyStr);
	
		BigInteger i1 = BigInteger.valueOf(1000000);
		String c1 = PaillierCipher.encrypt(i1, pubKey1);
		System.out.println("c1.length:"+c1.length());
		System.out.println("c1:"+c1);

		BigInteger o1 = PaillierCipher.decrypt(c1, priKey1);
		System.out.println("o1:"+o1);

		BigInteger i2 = BigInteger.valueOf(-20000);
		String c2 = PaillierCipher.encrypt(i2, pubKey1);
		System.out.println("c2.length:"+c2.length());
		System.out.println("c2:"+c2);

		BigInteger o2 = PaillierCipher.decrypt(c2, priKey1);
		System.out.println("o2:"+o2);

		String c3 = PaillierCipher.ciphertextAdd(c1,c2);
		System.out.println("c3.length:"+c3.length());
		System.out.println("c3:"+c3);
		
		BigInteger o3 = PaillierCipher.decrypt(c3, priKey1);
		System.out.println("o3:"+o3);		
	}

	public static void test2() {

		String publicKeyStr = 
			"-----BEGIN PUBLIC KEY-----\n"
			+ "MIGdMA0GCSqGSIb3DQEBAQUAA4GLADCBhwKBgQCM+yw34rFNq5sO6YHfg8uAggLl\n"
			+ "rGBbjFBN8tcxQZsZPiCGuj6/HubpxUxLlHlTntzTwgyla4tZJ3/K9AtQrIdnPNbI\n"
			+ "ByCYYggGkIeVfTtt7H647l8wI+nBGsrk44y1yKfzOfoIAgpOed4ghDXKYnJWOQw6\n"
			+ "DZjWZKVBF+RFi71dvwIBAw==\n"
			+ "-----END PUBLIC KEY-----";
		String privateKeyStr = 
			"-----BEGIN RSA PRIVATE KEY-----\n"
			+ "MIICdAIBADANBgkqhkiG9w0BAQEFAASCAl4wggJaAgEAAoGBAIz7LDfisU2rmw7p\n"
			+ "gd+Dy4CCAuWsYFuMUE3y1zFBmxk+IIa6Pr8e5unFTEuUeVOe3NPCDKVri1knf8r0\n"
			+ "C1Csh2c81sgHIJhiCAaQh5V9O23sfrjuXzAj6cEayuTjjLXIp/M5+ggCCk553iCE\n"
			+ "NcpiclY5DDoNmNZkpUEX5EWLvV2/AgEDAoGAXfzIJUHLiR0SCfEBP60yVawB7nLq\n"
			+ "57LgM/c6INZnZilrBHwp1L9Em9jdh7hQ4mnojSwIbkeyO2+qh01c4HME7n2b1INg\n"
			+ "WtYpv91RcHYGSrpfMY0rck6O9Jt6N3PPv7mowWiYtN+3aaa8hvi2FjrBtRycg4PQ\n"
			+ "E8xx/XTDjSwQk2sCQQDdcoXTAblbzHqwTdtQGEeMSrARWmflCE9wanqWatMS0HJj\n"
			+ "e7mqkS181nkiZbLxXGs+nvXP+KF5SALpZ3iBDAvxAkEAovqDbw5WxPzsFD+RfBm2\n"
			+ "SKU+iUOcyQsCwSkXH2pDH1peuZk/B+YCguzc7L72GPNbT4J+9+gXOLJB/1d5SJh0\n"
			+ "rwJBAJOhroyr0OfdpyAz54q62l2HIAuRmpiwNPWcUbmcjLc1oZen0RxgyP3kUMGZ\n"
			+ "IfY9nNRp+TVQa6YwAfDvpatdXUsCQGynAkoJjy398rgqYP1meYXDfwYtEzCyAdYb\n"
			+ "ZL+cLL+RlHu7f1qZVwHzPfMp+WX3kjUBqfqauiXMK/+PpjBlox8CQFbwyCgIxDRx\n"
			+ "UjM9FScI4B/LQykBCgdrPST+DmjN081qB/DVomtKUlvRPlYlpIiypXJVbAaxSPO/\n"
			+ "5PXtCouJdNQ=\n"
			+ "-----END RSA PRIVATE KEY-----";
	
		System.out.println("public key:" + publicKeyStr);
		System.out.println("private key:" + privateKeyStr);
		
		RSAPrivateKey priKey1 = (RSAPrivateKey)PaillierKeyPair.pemToPrivateKey(privateKeyStr);
	

		String c1 = 
				"00808cfb2c37e2b14dab9b0ee981df83cb808202e5ac605b8c504df2d731419b"
				+ "193e2086ba3ebf1ee6e9c54c4b9479539edcd3c20ca56b8b59277fcaf40b50ac"
				+ "87673cd6c80720986208069087957d3b6dec7eb8ee5f3023e9c11acae4e38cb5"
				+ "c8a7f339fa08020a4e79de208435ca627256390c3a0d98d664a54117e4458bbd"
				+ "5dbf3e8261ae9fbf003352cc10106b89bd04f731bf98cafc1bf70dd3bf31b0a9"
				+ "8ba4083f3451e4944da6e4096ca940ae1affd7fd791951ecd732f6705ff2709d"
				+ "1230de79cd5ef5091613e3ed76225907b4700083909baeb71e190d09ab1d9003"
				+ "cfcd02c0728e45f917a1b8419db7734869f3c585760ddb1be1603a4ab9914e40"
				+ "26cc0574c3a652915acf60a49c38e0650b16363c623bb0b35e80c9f5971405e7"
				+ "a03bfb69fbec875a46a3c698ad58fafc555c61bfef167c6d9b31b4ebbc866e01"
				+ "f549a091dc2a45cdd656cf48c0204863fc919fb886a82da3358cc1a2aa240004"
				+ "3014525cce23c05ca3b35cf9b0dde0c4c61728a0323c38a828a84ab77e63a625"
				+ "7c98";

		//Ëß£ÂØÜ
		BigInteger o1 = PaillierCipher.decrypt(c1, priKey1);
		System.out.println("o1:"+o1);


		String c2 = 
				"00808cfb2c37e2b14dab9b0ee981df83cb808202e5ac605b8c504df2d731419b"
				+ "193e2086ba3ebf1ee6e9c54c4b9479539edcd3c20ca56b8b59277fcaf40b50ac"
				+ "87673cd6c80720986208069087957d3b6dec7eb8ee5f3023e9c11acae4e38cb5"
				+ "c8a7f339fa08020a4e79de208435ca627256390c3a0d98d664a54117e4458bbd"
				+ "5dbf40079b7c6a5ff80aa77fcc6f0c5460e9243cef93c6f0cd262cc8f89248d6"
				+ "f54f1505a94b4d6d04bda0f208af9323153ae079e9db012ecbd3dee5a2596338"
				+ "a6be08c071a8a1483f2362b05881b63b1a0813a2ac658059cbc847e6bc90e1c9"
				+ "293f7ca3cc071791c9b64da8d7d7e43c86c845ab943ad67669579838385f6031"
				+ "0b75979236775059c51060dfd796af76377b1635b6d78a3542f0e2d30a77caca"
				+ "93c1a565725d8de98be7d1e6982c334dc8609fd65cb58b69865530bc653ea01d"
				+ "f5ec28a981ec8104f77fa8edf84d9ecfffde4a68a43b5867387956e1cb07a947"
				+ "718bd4df637f01cb14d07bebbb96d2ac407694a8e545987b202d58c713602235"
				+ "b906";

		BigInteger o2 = PaillierCipher.decrypt(c2, priKey1);
		System.out.println("o2:"+o2);

		//ÂêåÊ?ÅÂä†ÂØ?
		String c3 = PaillierCipher.ciphertextAdd(c1,c2);
		System.out.println("c3.length:"+c3.length());
		System.out.println("c3:"+c3);
		
		BigInteger o3 = PaillierCipher.decrypt(c3, priKey1);
		System.out.println("o3:"+o3);		
	}
	
	public static void main(String[] str){
		test1();
	}
}
