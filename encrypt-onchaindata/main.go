package main

import (
	"crypto/ecdsa"
	"crypto/x509"
	"encoding/base64"
	"encoding/hex"
	"encoding/pem"
	"flag"
	"fmt"
	"io/ioutil"

	"github.com/JcobCN/goEncrypt/ecc"
)

var (
	pemFile string
	crypt   string
	text    string
)

func init() {
	flag.StringVar(&pemFile, "f", "", "specific the PEM file")
	flag.StringVar(&crypt, "c", "", "crypt option, e:encrypt or d:decrypt")
	flag.StringVar(&text, "t", "", "in encrypt mode is plaintext, in decrypt mode is cipher text")
}

func readPem(pemPath string) ([]byte, error) {

	b, err := ioutil.ReadFile(pemPath)
	if err != nil {
		return nil, fmt.Errorf("readPem error: %v", err)
	}

	p, _ := pem.Decode(b)
	if p == nil {
		return nil, fmt.Errorf("pem Decode error 'p' is nil")
	}

	return p.Bytes, nil
}

func getEccKey(privateKey *ecdsa.PrivateKey) (ecc.EccKey, error) {
	privateBytes, err := x509.MarshalECPrivateKey(privateKey)
	if err != nil {
		return ecc.EccKey{}, err
	}
	publicBytes, err := x509.MarshalPKIXPublicKey(&privateKey.PublicKey)
	if err != nil {
		return ecc.EccKey{}, err
	}
	return ecc.EccKey{
		PrivateKey: base64.StdEncoding.EncodeToString(privateBytes),
		PublicKey:  base64.StdEncoding.EncodeToString(publicBytes),
	}, nil
}

func getEcdsaPrivateKeyBase64(privateKey *ecdsa.PrivateKey) (ecdsaPrivateKeyBase64 string, err error) {
	privateBytes, err := x509.MarshalECPrivateKey(privateKey)
	if err != nil {
		return "", err
	}
	return base64.StdEncoding.EncodeToString(privateBytes), nil
}

func getEcdsaPublicKeyBase64(publicKey *ecdsa.PublicKey) (ecdsaPublicKeyBase64 string, err error) {
	publicBytes, err := x509.MarshalPKIXPublicKey(publicKey)
	if err != nil {
		return "", err
	}
	return base64.StdEncoding.EncodeToString(publicBytes), nil
}

func s256Encrypt() error {

	fmt.Println("Start to Encrypt by secp256k1....")

	pubB, err := readPem(pemFile)
	if err != nil {
		return fmt.Errorf("read Pem error: %v", err)
	}

	// fmt.Println("publicKey is:" + hex.EncodeToString(pubB))
	tempPubKey, err := x509.ParsePKIXPublicKey(pubB)
	if err != nil {
		return fmt.Errorf("x509 prase pubkey error: %v", err)
	}
	ecdsaPub, ok := tempPubKey.(*ecdsa.PublicKey)
	if !ok {
		return fmt.Errorf("can't assert to *ecdsa.PublicKey")
	}
	ecdsaPubB64, err := getEcdsaPublicKeyBase64(ecdsaPub)
	if err != nil {
		return fmt.Errorf("getEcdsaPubKeyBase64 : %v", err)
	}

	cipherText, err := ecc.EccEncryptToBase64([]byte(text), ecdsaPubB64)
	if err != nil {
		return fmt.Errorf("encrypt error: %v", err)
	}
	fmt.Println("cipherText：" + cipherText)

	return nil
}

func s256Decrypt() error {

	fmt.Println("Start to Decrypt by secp256k1....")

	priB, err := readPem(pemFile)
	if err != nil {
		return fmt.Errorf("read Pem error: %v", err)
	}

	// fmt.Println("privateKey is:" + hex.EncodeToString(priB))
	tempPriKey, err := x509.ParsePKCS8PrivateKey(priB)
	if err != nil {
		return fmt.Errorf("x509 prase prikey error: %v", err)
	}
	ecdsaPri, ok := tempPriKey.(*ecdsa.PrivateKey)
	if !ok {
		return fmt.Errorf("can't assert to *ecdsa.PrivateKey")
	}
	ecdsaPriB64, err := getEcdsaPrivateKeyBase64(ecdsaPri)
	if err != nil {
		return fmt.Errorf("getEcdsaPriKeyBase64 : %v", err)
	}

	plainBytes, err := ecc.EccDecryptByBase64(text, ecdsaPriB64)
	if err != nil {
		return fmt.Errorf("decrypt error: %v", err)
	}
	fmt.Println("plainText：" + string(plainBytes))

	return nil
}

func test() {

	priKey, err := readPem("key.pem")
	if err != nil {
		fmt.Printf("read ")
	}

	hexs := hex.EncodeToString(priKey)
	fmt.Println("pem=" + hexs)

	tempPrivateKey, err := x509.ParsePKCS8PrivateKey(priKey)
	if err != nil {
		fmt.Printf("x509,%v\n", err)
		return
	}
	ecdsaPri, ok := tempPrivateKey.(*ecdsa.PrivateKey)
	if ok == false {
		fmt.Println("断言失败")
		return
	}

	eccPri := ecc.ImportECDSA(ecdsaPri)
	// eccPri.PublicKey.
	// eccPri.D.Bytes()
	fmt.Println(hex.EncodeToString(eccPri.D.Bytes()))
	fmt.Println(hex.EncodeToString(eccPri.PublicKey.Curve.Params().B.Bytes()))
	fmt.Println(hex.EncodeToString(eccPri.PublicKey.X.Bytes()))
	fmt.Println(hex.EncodeToString(eccPri.PublicKey.Y.Bytes()))

	pubB, err := readPem("pub.pem")
	if err != nil {
		fmt.Printf("read Pem error: %v\n", err)
	}

	fmt.Println(hex.EncodeToString(pubB))
	tempPubKey, err := x509.ParsePKIXPublicKey(pubB)
	if err != nil {
		fmt.Errorf("x509 pubKey %v\n", err)
	}
	ecdsaPub, ok := tempPubKey.(*ecdsa.PublicKey)
	if ok == false {
		fmt.Errorf("断言失败=\n")
		return
	}
	eccPub := ecc.ImportECDSAPublic(ecdsaPub)
	fmt.Println(hex.EncodeToString(eccPub.X.Bytes()))
	fmt.Println(hex.EncodeToString(eccPub.Y.Bytes()))

	eccKey, err := getEccKey(ecdsaPri)
	if err != nil {
		fmt.Printf("getEccKey error=%v\n", err)
		return
	}
	msg := "hello world"
	cipherText, err := ecc.EccEncryptToBase64([]byte(msg), eccKey.PublicKey)
	if err != nil {
		fmt.Printf("encrypt,%v\n", err)
		return
	}
	fmt.Println("密文：" + cipherText)

	resMsg, err := ecc.EccDecryptByBase64(cipherText, eccKey.PrivateKey)
	if err != nil {
		fmt.Printf("EccDecryptError=%v\n", err)
		return
	}
	fmt.Println(string(resMsg))
}

func main() {
	flag.Parse()
	// test()

	if len(text) == 0 {
		fmt.Println("Please enter 'text' flag -t , it must be need.")
		return
	}

	if crypt == "e" {
		err := s256Encrypt()
		if err != nil {
			fmt.Println(err)
		}
	} else if crypt == "d" {
		err := s256Decrypt()
		if err != nil {
			fmt.Println(err)
		}

	} else {
		fmt.Println("Please enter 'crypt' flag -c ")
		return
	}
}
