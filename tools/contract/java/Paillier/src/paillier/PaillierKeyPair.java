package paillier;

import java.io.IOException;
import java.io.StringReader;
import java.io.StringWriter;
import java.security.KeyFactory;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.NoSuchAlgorithmException;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.SecureRandom;
import java.security.spec.InvalidKeySpecException;
import java.security.spec.PKCS8EncodedKeySpec;
import java.security.spec.X509EncodedKeySpec;
import org.bouncycastle.util.io.pem.PemObject;
import org.bouncycastle.util.io.pem.PemObjectGenerator;
import org.bouncycastle.util.io.pem.PemWriter;

import org.bouncycastle.util.io.pem.PemReader;

public class PaillierKeyPair {
	public static KeyPair generateKeyPair(int bitLength) {
		KeyPairGenerator generator;
		try {
			generator = KeyPairGenerator.getInstance("RSA");
			generator.initialize(bitLength, new SecureRandom());  
		    return generator.generateKeyPair();  
		} catch (NoSuchAlgorithmException e) {
			e.printStackTrace();
		}
	    return null;
	}
	
	public static String publicKeyToPem(PublicKey publicKey) {
        StringWriter pemStrWriter = new StringWriter();
        PemWriter pemWriter = null;
        try {
            pemWriter = new PemWriter(pemStrWriter);
            PemObject pemObject = new PemObject("PUBLIC KEY", publicKey.getEncoded());
            pemWriter.writeObject(pemObject);
            pemWriter.flush();
            pemWriter.close();
            return pemStrWriter.toString();
        } catch (IOException e) {
			e.printStackTrace();
		}
		return null;
	}
	
	public static PublicKey pemToPublicKey(String publicKeyStr) {
        try {
        	StringReader pemStrReader = new StringReader(publicKeyStr);
        	PemReader pemReader = new PemReader(pemStrReader);
        	byte[] pubKey = pemReader.readPemObject().getContent();
        	pemReader.close();
        	
        	KeyFactory kf = KeyFactory.getInstance("RSA");
        	return kf.generatePublic(new X509EncodedKeySpec(pubKey));
        } catch (IOException e) {
			e.printStackTrace();
			return null;
        } catch (NoSuchAlgorithmException e) {
			e.printStackTrace();
        } catch (InvalidKeySpecException e) {
			e.printStackTrace();
		}
        return null;
	}
	
	public static String privateKeyToPem(PrivateKey privateKey) {
        try {
        	StringWriter pemStrWriter = new StringWriter();
        	PemWriter pemWriter = new PemWriter(pemStrWriter);
            PemObjectGenerator pemObjectGenerator = new PemObject("PRIVATE KEY", privateKey.getEncoded());
            pemWriter.writeObject(pemObjectGenerator);
            pemWriter.flush();
            pemWriter.close();
            return pemStrWriter.toString();
        } catch (IOException e) {
			e.printStackTrace();
			return null;
		}
	}
	
	public static PrivateKey pemToPrivateKey(String privateKeyStr) {
        try {
        	StringReader pemStrReader = new StringReader(privateKeyStr);
        	PemReader pemReader = new PemReader(pemStrReader);
        	byte[] priKey = pemReader.readPemObject().getContent();
        	pemReader.close();
        	
        	KeyFactory kf = KeyFactory.getInstance("RSA");
        	return kf.generatePrivate(new PKCS8EncodedKeySpec(priKey));
        } catch (IOException e) {
			e.printStackTrace();
			return null;
        } catch (NoSuchAlgorithmException e) {
			e.printStackTrace();
        } catch (InvalidKeySpecException e) {
			e.printStackTrace();
		}
        return null;
	}
}
