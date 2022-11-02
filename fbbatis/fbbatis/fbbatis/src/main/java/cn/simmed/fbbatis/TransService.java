package cn.simmed.fbbatis;

import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.fisco.bcos.sdk.abi.ABICodec;
import org.fisco.bcos.sdk.abi.ABICodecException;
import org.fisco.bcos.sdk.abi.datatypes.generated.tuples.generated.Tuple2;
import org.fisco.bcos.sdk.abi.wrapper.ABICodecJsonWrapper;
import org.fisco.bcos.sdk.abi.wrapper.ABIDefinition;
import org.fisco.bcos.sdk.abi.wrapper.ABIDefinitionFactory;
import org.fisco.bcos.sdk.abi.wrapper.ContractABIDefinition;
import org.fisco.bcos.sdk.client.Client;
import org.fisco.bcos.sdk.client.protocol.response.Call;
import org.fisco.bcos.sdk.crypto.CryptoSuite;
import org.fisco.bcos.sdk.crypto.keypair.CryptoKeyPair;
import org.fisco.bcos.sdk.crypto.signature.SignatureResult;
import org.fisco.bcos.sdk.model.TransactionReceipt;
import org.fisco.bcos.sdk.transaction.codec.decode.RevertMessageParser;
import org.fisco.bcos.sdk.transaction.codec.decode.TransactionDecoderService;
import org.fisco.bcos.sdk.transaction.manager.TransactionProcessor;
import org.fisco.bcos.sdk.transaction.manager.TransactionProcessorFactory;
import org.fisco.bcos.sdk.transaction.pusher.TransactionPusherService;
import org.fisco.bcos.sdk.utils.Numeric;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.stereotype.Service;

import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Collections;
import java.util.List;

import static cn.simmed.fbbatis.Constants.RECEIPT_STATUS_0X0;

@Slf4j
@Service
public class TransService {

    @Autowired
    private Web3ApiService web3ApiService;

    @Autowired
    private KeyStoreService keyStoreService;

    @Autowired
    @Qualifier(value = "common")
    private CryptoSuite cryptoSuite;

    public Object transHandleWithSign(int groupId, String userAddress,
                                      String contractAddress, String abiStr, String funcName, List<Object> funcParam)
            throws FrontException {
        // check groupId
        Client client = web3ApiService.getWeb3j(groupId);

        String encodeFunction = this.encodeFunction2Str(abiStr, funcName, funcParam);
        boolean isTxConstant = this.getABIDefinition(abiStr, funcName).isConstant();
        if (isTxConstant) {
            if (StringUtils.isBlank(userAddress)) {
                userAddress = keyStoreService.getCredentialsForQuery().getAddress();
            }
            return this.handleCall(groupId, userAddress, contractAddress, encodeFunction, abiStr, funcName);
        } else {
            return this.handleTransaction(client, keyStoreService.getCredentialsForQuery(), contractAddress, encodeFunction);
        }

    }

    public List<String> handleCall(int groupId, String userAddress, String contractAddress,
                                   String encodedFunction, String abiStr, String funcName) {

        TransactionProcessor transactionProcessor = new TransactionProcessor(
                web3ApiService.getWeb3j(groupId), keyStoreService.getCredentialsForQuery(),
                groupId, Constants.chainId);
        Call.CallOutput callOutput = transactionProcessor
                .executeCall(userAddress, contractAddress, encodedFunction)
                .getCallResult();
        // if error
        if (!RECEIPT_STATUS_0X0.equals(callOutput.getStatus())) {
            Tuple2<Boolean, String> parseResult =
                    RevertMessageParser.tryResolveRevertMessage(callOutput.getStatus(), callOutput.getOutput());
            log.error("call contract error:{}", parseResult);
            String parseResultStr = parseResult.getValue1() ? parseResult.getValue2() : "call contract error of status" + callOutput.getStatus();
            return Collections.singletonList("Call contract return error: " + parseResultStr);
        } else {
            ABICodec abiCodec = new ABICodec(cryptoSuite);
            try {
                List<String> res = abiCodec
                        .decodeMethodToString(abiStr, funcName, callOutput.getOutput());
                log.info("call contract res before decode:{}", res);
                // bytes类型转十六进制
                this.handleFuncOutput(abiStr, funcName, res);
                log.info("call contract res:{}", res);
                return res;
            } catch (ABICodecException e) {
                log.error("handleCall decode call output fail:[]", e);
                throw new FrontException(ConstantCode.CONTRACT_TYPE_DECODED_ERROR);
            }
        }
    }


    public TransactionReceipt handleTransaction(Client client, CryptoKeyPair cryptoKeyPair, String contractAddress,
                                                String encodeFunction) {
        Instant startTime = Instant.now();
        log.info("handleTransaction start startTime:{}", startTime.toEpochMilli());
        // send tx
        TransactionProcessor txProcessor = TransactionProcessorFactory.createTransactionProcessor(client, cryptoKeyPair);
        TransactionReceipt receipt = txProcessor.sendTransactionAndGetReceipt(contractAddress, encodeFunction, cryptoKeyPair);
        // cover null message through statusCode
        this.decodeReceipt(receipt);
        log.info("execTransaction end  useTime:{}",
                Duration.between(startTime, Instant.now()).toMillis());
        return receipt;
    }


//    public TransactionReceipt handleTransaction(Client client, String userAddress, String contractAddress, String encodeFunction) {
//        log.debug("handleTransaction userAddress:{},contractAddress:{},encodeFunction:{}",userAddress,contractAddress, encodeFunction);
//        // raw tx
//        Pair<String, Integer> chainIdAndGroupId = TransactionProcessorFactory.getChainIdAndGroupId(client);
//        TransactionBuilderInterface transactionBuilder = new TransactionBuilderService(client);
//        RawTransaction rawTransaction = transactionBuilder.createTransaction(DefaultGasProvider.GAS_PRICE,
//                DefaultGasProvider.GAS_LIMIT, contractAddress, encodeFunction,
//                BigInteger.ZERO, new BigInteger(chainIdAndGroupId.getLeft()),
//                BigInteger.valueOf(chainIdAndGroupId.getRight()), "");
//        // encode
//        TransactionEncoderInterface transactionEncoder = new TransactionEncoderService(cryptoSuite);
//        byte[] encodedTransaction = transactionEncoder.encode(rawTransaction, null);
//        // sign
//        SignatureResult signResult = this.signData(encodedTransaction, userAddress);
//        byte[] signedMessage = transactionEncoder.encode(rawTransaction, signResult);
//        String signedMessageStr = Numeric.toHexString(signedMessage);
//
//        Instant nodeStartTime = Instant.now();
//        // send transaction
//        TransactionReceipt receipt = sendMessage(client, signedMessageStr);
//        log.info("***node cost time***: {}",
//                Duration.between(nodeStartTime, Instant.now()).toMillis());
//        return receipt;
//
//    }

    public SignatureResult signData(byte[] dataToSign,String userAddress){
        SignatureResult signatureResult = cryptoSuite.sign(dataToSign, keyStoreService.getCredentialsForQuery());
        return signatureResult;
    }

    /**
     * send message to node.
     *
     * @param signMsg signMsg
     */
    public TransactionReceipt sendMessage(Client client, String signMsg) {
        TransactionPusherService txPusher = new TransactionPusherService(client);
        TransactionReceipt receipt = txPusher.push(signMsg);
        this.decodeReceipt(receipt);
        return receipt;

    }


    public void decodeReceipt(TransactionReceipt receipt) {
        // decode receipt
        TransactionDecoderService txDecoder = new TransactionDecoderService(cryptoSuite);
        String receiptMsg = txDecoder.decodeReceiptStatus(receipt).getReceiptMessages();
        receipt.setMessage(receiptMsg);
        CommonUtils.processReceiptHexNumber(receipt);
    }

    public String encodeFunction2Str(String abiStr, String funcName, List<Object> funcParam) {

        funcParam = funcParam == null ? new ArrayList<>() : funcParam;
        this.validFuncParam(abiStr, funcName, funcParam);
        ABICodec abiCodec = new ABICodec(cryptoSuite);
        String encodeFunction;
        try {
            encodeFunction = abiCodec.encodeMethod(abiStr, funcName, funcParam);
        } catch (ABICodecException e) {
            log.error("transHandleWithSign encode fail:[]", e);
            throw new FrontException(ConstantCode.CONTRACT_TYPE_ENCODED_ERROR);
        }
        log.info("encodeFunction2Str encodeFunction:{}", encodeFunction);
        return encodeFunction;
    }


    private ABIDefinition getABIDefinition(String abiStr, String functionName) {
        ABIDefinitionFactory factory = new ABIDefinitionFactory(cryptoSuite);

        ContractABIDefinition contractABIDefinition = factory.loadABI(abiStr);
        List<ABIDefinition> abiDefinitionList = contractABIDefinition.getFunctions()
                .get(functionName);
        if (abiDefinitionList.isEmpty()) {
            throw new FrontException(ConstantCode.IN_FUNCTION_ERROR);
        }
        // abi only contain one function, so get first one
        ABIDefinition function = abiDefinitionList.get(0);
        return function;
    }

    private void validFuncParam(String contractAbiStr, String funcName, List<Object> funcParam) {
        ABIDefinition abiDefinition = this.getABIDefinition(contractAbiStr, funcName);
        List<ABIDefinition.NamedType> inputTypeList = abiDefinition.getInputs();
        if (inputTypeList.size() != funcParam.size()) {
            log.error("validFuncParam param not match");
            throw new FrontException(ConstantCode.FUNC_PARAM_SIZE_NOT_MATCH);
        }
        for (int i = 0; i < inputTypeList.size(); i++) {
            String type = inputTypeList.get(i).getType();
            if (type.startsWith("bytes")) {
                if (type.contains("[][]")) {
                    // todo bytes[][]
                    log.warn("validFuncParam param, not support bytes 2d array or more");
                    throw new FrontException(ConstantCode.FUNC_PARAM_BYTES_NOT_SUPPORT_HIGH_D);
                }
                // if not bytes[], bytes or bytesN
                if (!type.endsWith("[]")) {
                    // update funcParam
                    String bytesHexStr = (String) (funcParam.get(i));
                    byte[] inputArray = Numeric.hexStringToByteArray(bytesHexStr);
                    // bytesN: bytes1, bytes32 etc.
                    if (type.length() > "bytes".length()) {
                        int bytesNLength = Integer.parseInt(type.substring("bytes".length()));
                        if (inputArray.length != bytesNLength) {
                            log.error("validFuncParam param of bytesN size not match");
                            throw new FrontException(ConstantCode.FUNC_PARAM_BYTES_SIZE_NOT_MATCH);
                        }
                    }
                    // replace hexString with array
                    funcParam.set(i, inputArray);
                } else {
                    // if bytes[] or bytes32[]
                    List<String> hexStrArray = (List<String>) (funcParam.get(i));
                    List<byte[]> bytesArray = new ArrayList<>(hexStrArray.size());
                    for (int j = 0; j < hexStrArray.size(); j++) {
                        String bytesHexStr = hexStrArray.get(j);
                        byte[] inputArray = Numeric.hexStringToByteArray(bytesHexStr);
                        // check: bytesN: bytes1, bytes32 etc.
                        if (type.length() > "bytes[]".length()) {
                            // bytes32[] => 32[]
                            String temp = type.substring("bytes".length());
                            // 32[] => 32
                            int bytesNLength = Integer
                                    .parseInt(temp.substring(0, temp.length() - 2));
                            if (inputArray.length != bytesNLength) {
                                log.error("validFuncParam param of bytesN size not match");
                                throw new FrontException(
                                        ConstantCode.FUNC_PARAM_BYTES_SIZE_NOT_MATCH);
                            }
                        }
                        bytesArray.add(inputArray);
                    }
                    // replace hexString with array
                    funcParam.set(i, bytesArray);
                }
            }
        }
    }

    /**
     * parse bytes, bytesN, bytesN[], bytes[] from base64 string to hex string
     * @param abiStr
     * @param funcName
     * @param outputValues
     */
    private void handleFuncOutput(String abiStr, String funcName, List<String> outputValues) {
        ABIDefinition abiDefinition = this.getABIDefinition(abiStr, funcName);
        ABICodecJsonWrapper jsonWrapper = new ABICodecJsonWrapper();
        List<ABIDefinition.NamedType> outputTypeList = abiDefinition.getOutputs();
        for (int i = 0; i < outputTypeList.size(); i++) {
            String type = outputTypeList.get(i).getType();
            if (type.startsWith("bytes")) {
                if (type.contains("[][]")) { // todo bytes[][]
                    log.warn("validFuncParam param, not support bytes 2d array or more");
                    continue;
                }
                // if not bytes[], bytes or bytesN
                if (!type.endsWith("[]")) {
                    // update funcParam
                    String bytesBase64Str = outputValues.get(i);
                    if (bytesBase64Str.startsWith("base64://")) {
                        bytesBase64Str = bytesBase64Str.substring("base64://".length());
                    }
                    byte[] inputArray = Base64.getDecoder().decode(bytesBase64Str);
                    // replace hexString with array
                    outputValues.set(i, Numeric.toHexString(inputArray));
                } else {
                    // if bytes[] or bytes32[]
                    List<String> base64StrArray = JsonUtils.toJavaObject(outputValues.get(i), List.class);
                    List<String> bytesArray = new ArrayList<>(base64StrArray.size());
                    for (int j = 0; j < base64StrArray.size(); j++) {
                        String bytesBase64Str = base64StrArray.get(j);
                        if (bytesBase64Str.startsWith("base64://")) {
                            bytesBase64Str = bytesBase64Str.substring("base64://".length());
                        }
                        byte[] inputArray = Base64.getDecoder().decode(bytesBase64Str);
                        bytesArray.add(Numeric.toHexString(inputArray));
                    }
                    // replace hexString with array
                    outputValues.set(i, JsonUtils.objToString(bytesArray));
                }
            }
        }
    }

}
