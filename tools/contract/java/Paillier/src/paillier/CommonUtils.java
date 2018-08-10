package paillier;

import java.math.BigInteger;

public class CommonUtils {
	public CommonUtils() {
	}

    public static byte[] intToByte4(int i) {    
        byte[] targets = new byte[4];    
        targets[3] = (byte) (i & 0xFF);    
        targets[2] = (byte) (i >> 8 & 0xFF);    
        targets[1] = (byte) (i >> 16 & 0xFF);    
        targets[0] = (byte) (i >> 24 & 0xFF);    
        return targets;    
    }    
    
    public static byte[] longToByte8(long lo) {    
        byte[] targets = new byte[8];    
        for (int i = 0; i < 8; i++) {    
            int offset = (targets.length - 1 - i) * 8;    
            targets[i] = (byte) ((lo >>> offset) & 0xFF);    
        }    
        return targets;    
    }
    
    public static byte[] unsignedShortToByte2(int s) {    
        byte[] targets = new byte[2];    
        targets[0] = (byte) (s >> 8 & 0xFF);    
        targets[1] = (byte) (s & 0xFF);    
        return targets;    
    }    
    
    public static int byte2ToUnsignedShort(byte[] bytes) {    
        return byte2ToUnsignedShort(bytes, 0);    
    }    
    
    public static int byte2ToUnsignedShort(byte[] bytes, int off) {    
        int high = bytes[off];    
        int low = bytes[off + 1];    
        return (high << 8 & 0xFF00) | (low & 0xFF);    
    }
    
    public static int byte4ToInt(byte[] bytes, int off) {    
        int b0 = bytes[off] & 0xFF;    
        int b1 = bytes[off + 1] & 0xFF;    
        int b2 = bytes[off + 2] & 0xFF;    
        int b3 = bytes[off + 3] & 0xFF;    
        return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;    
    }

	public static byte[] asUnsignedByteArray(BigInteger paramBigInteger) {
		byte[] arrayOfByte1 = paramBigInteger.toByteArray();
		if (arrayOfByte1[0] == 0) {
			byte[] arrayOfByte2 = new byte[arrayOfByte1.length - 1];
			System.arraycopy(arrayOfByte1, 1, arrayOfByte2, 0, arrayOfByte2.length);
			return arrayOfByte2;
		}
		return arrayOfByte1;
	}
	
	public static byte[] asUnsignedByteArray(BigInteger paramBigInteger, int byteLength) {
		byte[] arrayOfByte1 = asUnsignedByteArray(paramBigInteger);
		if(arrayOfByte1.length < byteLength) {
			byte[] arrayOfByte2 = new byte[byteLength];
			int offset = byteLength - arrayOfByte1.length;
			for(int i=0; i<offset; i++) {
				arrayOfByte2[i] = 0;
			}
			System.arraycopy(arrayOfByte1, 0, arrayOfByte2, offset, arrayOfByte1.length);
			return arrayOfByte2;
		}
		return arrayOfByte1;
	}
	
	public static BigInteger fromUnsignedByteArray(byte[] paramArrayOfByte) {
		return new BigInteger(1, paramArrayOfByte);
	}

	public static String byteToHexString(byte[] bt) {
		StringBuffer sb = new StringBuffer();
		for (int i = 0; i < bt.length; i++) {
			String hex = Integer.toHexString(bt[i] & 0xFF);
			if (hex.length() == 1) {
				hex = '0' + hex;
			}
			sb.append(hex.toUpperCase());
		}
		return sb.toString();
	}
	
	public static byte[] hexStringToBytes(String hexString) {   
	    if (hexString == null || hexString.equals("")) {   
	        return null;   
	    }   
	    hexString = hexString.toUpperCase();   
	    int length = hexString.length() / 2;   
	    char[] hexChars = hexString.toCharArray();   
	    byte[] d = new byte[length];   
	    for (int i = 0; i < length; i++) {   
	        int pos = i * 2;   
	        d[i] = (byte) (charToByte(hexChars[pos]) << 4 | charToByte(hexChars[pos + 1]));   
	    }   
	    return d;   
	}   
	
	private static byte charToByte(char c) {   
	    return (byte) "0123456789ABCDEF".indexOf(c);   
	}
}
