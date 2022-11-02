package cn.simmed.fbbatis;

import com.fasterxml.jackson.databind.JsonNode;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.fisco.bcos.sdk.crypto.CryptoSuite;
import org.fisco.bcos.sdk.crypto.keypair.CryptoKeyPair;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.http.HttpEntity;
import org.springframework.http.HttpHeaders;
import org.springframework.stereotype.Service;
import org.springframework.web.client.HttpStatusCodeException;
import org.springframework.web.client.ResourceAccessException;

import java.util.Objects;

@Slf4j
@Service
public class KeyStoreService {
    @Autowired
    @Qualifier(value = "common")
    private CryptoSuite cryptoSuite;

    public CryptoKeyPair getCredentialsForQuery() {
        log.debug("start getCredentialsForQuery. ");
        // create keyPair(support guomi)
        CryptoKeyPair keyPair = cryptoSuite.getKeyPairFactory().generateKeyPair();
        if (keyPair == null) {
            log.error("create random Credentials for query failed for null key pair");
            throw new FrontException(ConstantCode.WEB3J_CREATE_KEY_PAIR_NULL);
        }
        // keyPair2KeyStoreInfo(keyPair, "");
        return keyPair;
    }
}
