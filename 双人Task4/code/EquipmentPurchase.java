package org.fisco.bcos.sdk.demo.contract;

import java.math.BigInteger;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import org.fisco.bcos.sdk.abi.FunctionReturnDecoder;
import org.fisco.bcos.sdk.abi.TypeReference;
import org.fisco.bcos.sdk.abi.datatypes.Function;
import org.fisco.bcos.sdk.abi.datatypes.Type;
import org.fisco.bcos.sdk.abi.datatypes.Utf8String;
import org.fisco.bcos.sdk.abi.datatypes.generated.Uint256;
import org.fisco.bcos.sdk.abi.datatypes.generated.tuples.generated.Tuple1;
import org.fisco.bcos.sdk.abi.datatypes.generated.tuples.generated.Tuple2;
import org.fisco.bcos.sdk.client.Client;
import org.fisco.bcos.sdk.contract.Contract;
import org.fisco.bcos.sdk.crypto.CryptoSuite;
import org.fisco.bcos.sdk.crypto.keypair.CryptoKeyPair;
import org.fisco.bcos.sdk.model.CryptoType;
import org.fisco.bcos.sdk.model.TransactionReceipt;
import org.fisco.bcos.sdk.model.callback.TransactionCallback;
import org.fisco.bcos.sdk.transaction.model.exception.ContractException;

@SuppressWarnings("unchecked")
public class EquipmentPurchase extends Contract {
    public static final String[] BINARY_ARRAY = {
        "60806040526110066000806101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550600a60035534801561005757600080fd5b50610afa806100676000396000f300608060405260043610610098576000357c0100000000000000000000000000000000000000000000000000000000900463ffffffff168062f0e1331461009d5780631ed8d2c41461010657806360c477571461018357806366060a94146101f6578063748e7a1b1461027357806394618e4c1461028a578063b4c653e0146102a1578063c17e1efe14610314578063d86eb57e1461033f575b600080fd5b3480156100a957600080fd5b50610104600480360381019080803590602001908201803590602001908080601f01602080910402602001604051908101604052809392919081815260200183838082843782019150505050505091929192905050506103a5565b005b34801561011257600080fd5b5061016d600480360381019080803590602001908201803590602001908080601f016020809104026020016040519081016040528093929190818152602001838380828437820191505050505050919291929050505061050d565b6040518082815260200191505060405180910390f35b34801561018f57600080fd5b506101f4600480360381019080803590602001908201803590602001908080601f016020809104026020016040519081016040528093929190818152602001838380828437820191505050505050919291929080359060200190929190505050610582565b005b34801561020257600080fd5b5061025d600480360381019080803590602001908201803590602001908080601f0160208091040260200160405190810160405280939291908181526020018383808284378201915050505050509192919290505050610765565b6040518082815260200191505060405180910390f35b34801561027f57600080fd5b506102886107da565b005b34801561029657600080fd5b5061029f610858565b005b3480156102ad57600080fd5b50610312600480360381019080803590602001908201803590602001908080601f0160208091040260200160405190810160405280939291908181526020018383808284378201915050505050509192919290803590602001909291905050506108da565b005b34801561032057600080fd5b50610329610a4b565b6040518082815260200191505060405180910390f35b6103a3600480360381019080803590602001908201803590602001908080601f016020809104026020016040519081016040528093929190818152602001838380828437820191505050505050919291929080359060200190929190505050610a51565b005b6000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1663714c65bd30836040518363ffffffff167c0100000000000000000000000000000000000000000000000000000000028152600401808373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200180602001828103825283818151815260200191508051906020019080838360005b83811015610481578082015181840152602081019050610466565b50505050905090810190601f1680156104ae5780820380516001836020036101000a031916815260200191505b509350505050602060405180830381600087803b1580156104ce57600080fd5b505af11580156104e2573d6000803e3d6000fd5b505050506040513d60208110156104f857600080fd5b81019080805190602001909291905050505050565b60006001826040518082805190602001908083835b6020831015156105475780518252602082019150602081019050602083039250610522565b6001836020036101000a0380198251168184511680821785525050505050509050019150509081526020016040518091039020549050919050565b600081600354029050806001846040518082805190602001908083835b6020831015156105c4578051825260208201915060208101905060208303925061059f565b6001836020036101000a0380198251168184511680821785525050505050509050019150509081526020016040518091039020541015151561066e576040517fc703cb120000000000000000000000000000000000000000000000000000000081526004018080602001828103825260148152602001807f496e73756666696369656e742062616c616e636500000000000000000000000081525060200191505060405180910390fd5b806001846040518082805190602001908083835b6020831015156106a75780518252602082019150602081019050602083039250610682565b6001836020036101000a038019825116818451168082178552505050505050905001915050908152602001604051809103902060008282540392505081905550816002846040518082805190602001908083835b60208310151561072057805182526020820191506020810190506020830392506106fb565b6001836020036101000a038019825116818451168082178552505050505050905001915050908152602001604051809103902060008282540192505081905550505050565b60006002826040518082805190602001908083835b60208310151561079f578051825260208201915060208101905060208303925061077a565b6001836020036101000a0380198251168184511680821785525050505050509050019150509081526020016040518091039020549050919050565b6108186040805190810160405280601781526020017f6465706f73697428737472696e672c75696e74323536290000000000000000008152506103a5565b6108566040805190810160405280601c81526020017f62757945717569706d656e7428737472696e672c75696e7432353629000000008152506103a5565b565b6108986040805190810160405280601781526020017f6465706f73697428737472696e672c75696e743235362900000000000000000081525060026108da565b6108d86040805190810160405280601c81526020017f62757945717569706d656e7428737472696e672c75696e74323536290000000081525060026108da565b565b6000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1663dc536a623084846040518463ffffffff167c0100000000000000000000000000000000000000000000000000000000028152600401808473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200180602001838152602001828103825284818151815260200191508051906020019080838360005b838110156109bd5780820151818401526020810190506109a2565b50505050905090810190601f1680156109ea5780820380516001836020036101000a031916815260200191505b50945050505050602060405180830381600087803b158015610a0b57600080fd5b505af1158015610a1f573d6000803e3d6000fd5b505050506040513d6020811015610a3557600080fd5b8101908080519060200190929190505050505050565b60035481565b806001836040518082805190602001908083835b602083101515610a8a5780518252602082019150602081019050602083039250610a65565b6001836020036101000a03801982511681845116808217855250505050505090500191505090815260200160405180910390206000828254019250508190555050505600a165627a7a72305820021f6861bb121ba9ebb807d8cf28e654d75770c65032ba36bb19e55b478b6d560029"
    };

    public static final String BINARY =
            org.fisco.bcos.sdk.utils.StringUtils.joinAll("", BINARY_ARRAY);

    public static final String[] SM_BINARY_ARRAY = {
        "60806040526110066000806101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550600a60035534801561005757600080fd5b50610afa806100676000396000f300608060405260043610610098576000357c0100000000000000000000000000000000000000000000000000000000900463ffffffff168062f0e1331461009d5780631ed8d2c41461010657806360c477571461018357806366060a94146101f6578063748e7a1b1461027357806394618e4c1461028a578063b4c653e0146102a1578063c17e1efe14610314578063d86eb57e1461033f575b600080fd5b3480156100a957600080fd5b50610104600480360381019080803590602001908201803590602001908080601f01602080910402602001604051908101604052809392919081815260200183838082843782019150505050505091929192905050506103a5565b005b34801561011257600080fd5b5061016d600480360381019080803590602001908201803590602001908080601f016020809104026020016040519081016040528093929190818152602001838380828437820191505050505050919291929050505061050d565b6040518082815260200191505060405180910390f35b34801561018f57600080fd5b506101f4600480360381019080803590602001908201803590602001908080601f016020809104026020016040519081016040528093929190818152602001838380828437820191505050505050919291929080359060200190929190505050610582565b005b34801561020257600080fd5b5061025d600480360381019080803590602001908201803590602001908080601f0160208091040260200160405190810160405280939291908181526020018383808284378201915050505050509192919290505050610765565b6040518082815260200191505060405180910390f35b34801561027f57600080fd5b506102886107da565b005b34801561029657600080fd5b5061029f610858565b005b3480156102ad57600080fd5b50610312600480360381019080803590602001908201803590602001908080601f0160208091040260200160405190810160405280939291908181526020018383808284378201915050505050509192919290803590602001909291905050506108da565b005b34801561032057600080fd5b50610329610a4b565b6040518082815260200191505060405180910390f35b6103a3600480360381019080803590602001908201803590602001908080601f016020809104026020016040519081016040528093929190818152602001838380828437820191505050505050919291929080359060200190929190505050610a51565b005b6000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1663714c65bd30836040518363ffffffff167c0100000000000000000000000000000000000000000000000000000000028152600401808373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200180602001828103825283818151815260200191508051906020019080838360005b83811015610481578082015181840152602081019050610466565b50505050905090810190601f1680156104ae5780820380516001836020036101000a031916815260200191505b509350505050602060405180830381600087803b1580156104ce57600080fd5b505af11580156104e2573d6000803e3d6000fd5b505050506040513d60208110156104f857600080fd5b81019080805190602001909291905050505050565b60006001826040518082805190602001908083835b6020831015156105475780518252602082019150602081019050602083039250610522565b6001836020036101000a0380198251168184511680821785525050505050509050019150509081526020016040518091039020549050919050565b600081600354029050806001846040518082805190602001908083835b6020831015156105c4578051825260208201915060208101905060208303925061059f565b6001836020036101000a0380198251168184511680821785525050505050509050019150509081526020016040518091039020541015151561066e576040517fc703cb120000000000000000000000000000000000000000000000000000000081526004018080602001828103825260148152602001807f496e73756666696369656e742062616c616e636500000000000000000000000081525060200191505060405180910390fd5b806001846040518082805190602001908083835b6020831015156106a75780518252602082019150602081019050602083039250610682565b6001836020036101000a038019825116818451168082178552505050505050905001915050908152602001604051809103902060008282540392505081905550816002846040518082805190602001908083835b60208310151561072057805182526020820191506020810190506020830392506106fb565b6001836020036101000a038019825116818451168082178552505050505050905001915050908152602001604051809103902060008282540192505081905550505050565b60006002826040518082805190602001908083835b60208310151561079f578051825260208201915060208101905060208303925061077a565b6001836020036101000a0380198251168184511680821785525050505050509050019150509081526020016040518091039020549050919050565b6108186040805190810160405280601781526020017f6465706f73697428737472696e672c75696e74323536290000000000000000008152506103a5565b6108566040805190810160405280601c81526020017f62757945717569706d656e7428737472696e672c75696e7432353629000000008152506103a5565b565b6108986040805190810160405280601781526020017f6465706f73697428737472696e672c75696e743235362900000000000000000081525060026108da565b6108d86040805190810160405280601c81526020017f62757945717569706d656e7428737472696e672c75696e74323536290000000081525060026108da565b565b6000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1663dc536a623084846040518463ffffffff167c0100000000000000000000000000000000000000000000000000000000028152600401808473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200180602001838152602001828103825284818151815260200191508051906020019080838360005b838110156109bd5780820151818401526020810190506109a2565b50505050905090810190601f1680156109ea5780820380516001836020036101000a031916815260200191505b50945050505050602060405180830381600087803b158015610a0b57600080fd5b505af1158015610a1f573d6000803e3d6000fd5b505050506040513d6020811015610a3557600080fd5b8101908080519060200190929190505050505050565b60035481565b806001836040518082805190602001908083835b602083101515610a8a5780518252602082019150602081019050602083039250610a65565b6001836020036101000a03801982511681845116808217855250505050505090500191505090815260200160405180910390206000828254019250508190555050505600a165627a7a72305820021f6861bb121ba9ebb807d8cf28e654d75770c65032ba36bb19e55b478b6d560029"
    };

    public static final String SM_BINARY =
            org.fisco.bcos.sdk.utils.StringUtils.joinAll("", SM_BINARY_ARRAY);

    public static final String[] ABI_ARRAY = {
        "[{\"methodSignatureAsString\":\"unregisterParallelFunction(string)\",\"name\":\"unregisterParallelFunction\",\"type\":\"function\",\"constant\":false,\"payable\":false,\"anonymous\":false,\"stateMutability\":\"nonpayable\",\"inputs\":[{\"name\":\"functionName\",\"type\":\"string\",\"internalType\":\"\",\"indexed\":false,\"components\":[],\"dynamic\":true,\"typeAsString\":\"string\"}],\"outputs\":[]},{\"methodSignatureAsString\":\"getUserBalance(string)\",\"name\":\"getUserBalance\",\"type\":\"function\",\"constant\":true,\"payable\":false,\"anonymous\":false,\"stateMutability\":\"view\",\"inputs\":[{\"name\":\"username\",\"type\":\"string\",\"internalType\":\"\",\"indexed\":false,\"components\":[],\"dynamic\":true,\"typeAsString\":\"string\"}],\"outputs\":[{\"name\":\"\",\"type\":\"uint256\",\"internalType\":\"\",\"indexed\":false,\"components\":[],\"dynamic\":false,\"typeAsString\":\"uint256\"}]},{\"methodSignatureAsString\":\"buyEquipment(string,uint256)\",\"name\":\"buyEquipment\",\"type\":\"function\",\"constant\":false,\"payable\":false,\"anonymous\":false,\"stateMutability\":\"nonpayable\",\"inputs\":[{\"name\":\"username\",\"type\":\"string\",\"internalType\":\"\",\"indexed\":false,\"components\":[],\"dynamic\":true,\"typeAsString\":\"string\"},{\"name\":\"quantity\",\"type\":\"uint256\",\"internalType\":\"\",\"indexed\":false,\"components\":[],\"dynamic\":false,\"typeAsString\":\"uint256\"}],\"outputs\":[]},{\"methodSignatureAsString\":\"getEquipmentInventory(string)\",\"name\":\"getEquipmentInventory\",\"type\":\"function\",\"constant\":true,\"payable\":false,\"anonymous\":false,\"stateMutability\":\"view\",\"inputs\":[{\"name\":\"username\",\"type\":\"string\",\"internalType\":\"\",\"indexed\":false,\"components\":[],\"dynamic\":true,\"typeAsString\":\"string\"}],\"outputs\":[{\"name\":\"\",\"type\":\"uint256\",\"internalType\":\"\",\"indexed\":false,\"components\":[],\"dynamic\":false,\"typeAsString\":\"uint256\"}]},{\"methodSignatureAsString\":\"disableParallel()\",\"name\":\"disableParallel\",\"type\":\"function\",\"constant\":false,\"payable\":false,\"anonymous\":false,\"stateMutability\":\"nonpayable\",\"inputs\":[],\"outputs\":[]},{\"methodSignatureAsString\":\"enableParallel()\",\"name\":\"enableParallel\",\"type\":\"function\",\"constant\":false,\"payable\":false,\"anonymous\":false,\"stateMutability\":\"nonpayable\",\"inputs\":[],\"outputs\":[]},{\"methodSignatureAsString\":\"registerParallelFunction(string,uint256)\",\"name\":\"registerParallelFunction\",\"type\":\"function\",\"constant\":false,\"payable\":false,\"anonymous\":false,\"stateMutability\":\"nonpayable\",\"inputs\":[{\"name\":\"functionName\",\"type\":\"string\",\"internalType\":\"\",\"indexed\":false,\"components\":[],\"dynamic\":true,\"typeAsString\":\"string\"},{\"name\":\"criticalSize\",\"type\":\"uint256\",\"internalType\":\"\",\"indexed\":false,\"components\":[],\"dynamic\":false,\"typeAsString\":\"uint256\"}],\"outputs\":[]},{\"methodSignatureAsString\":\"equipmentPrice()\",\"name\":\"equipmentPrice\",\"type\":\"function\",\"constant\":true,\"payable\":false,\"anonymous\":false,\"stateMutability\":\"view\",\"inputs\":[],\"outputs\":[{\"name\":\"\",\"type\":\"uint256\",\"internalType\":\"\",\"indexed\":false,\"components\":[],\"dynamic\":false,\"typeAsString\":\"uint256\"}]},{\"methodSignatureAsString\":\"deposit(string,uint256)\",\"name\":\"deposit\",\"type\":\"function\",\"constant\":false,\"payable\":true,\"anonymous\":false,\"stateMutability\":\"payable\",\"inputs\":[{\"name\":\"username\",\"type\":\"string\",\"internalType\":\"\",\"indexed\":false,\"components\":[],\"dynamic\":true,\"typeAsString\":\"string\"},{\"name\":\"num\",\"type\":\"uint256\",\"internalType\":\"\",\"indexed\":false,\"components\":[],\"dynamic\":false,\"typeAsString\":\"uint256\"}],\"outputs\":[]}]"
    };

    public static final String ABI = org.fisco.bcos.sdk.utils.StringUtils.joinAll("", ABI_ARRAY);

    public static final String FUNC_UNREGISTERPARALLELFUNCTION = "unregisterParallelFunction";

    public static final String FUNC_GETUSERBALANCE = "getUserBalance";

    public static final String FUNC_BUYEQUIPMENT = "buyEquipment";

    public static final String FUNC_GETEQUIPMENTINVENTORY = "getEquipmentInventory";

    public static final String FUNC_DISABLEPARALLEL = "disableParallel";

    public static final String FUNC_ENABLEPARALLEL = "enableParallel";

    public static final String FUNC_REGISTERPARALLELFUNCTION = "registerParallelFunction";

    public static final String FUNC_EQUIPMENTPRICE = "equipmentPrice";

    public static final String FUNC_DEPOSIT = "deposit";

    protected EquipmentPurchase(String contractAddress, Client client, CryptoKeyPair credential) {
        super(getBinary(client.getCryptoSuite()), contractAddress, client, credential);
    }

    public static String getBinary(CryptoSuite cryptoSuite) {
        return (cryptoSuite.getCryptoTypeConfig() == CryptoType.ECDSA_TYPE ? BINARY : SM_BINARY);
    }

    public TransactionReceipt unregisterParallelFunction(String functionName) {
        final Function function =
                new Function(
                        FUNC_UNREGISTERPARALLELFUNCTION,
                        Arrays.<Type>asList(
                                new org.fisco.bcos.sdk.abi.datatypes.Utf8String(functionName)),
                        Collections.<TypeReference<?>>emptyList());
        return executeTransaction(function);
    }

    public byte[] unregisterParallelFunction(String functionName, TransactionCallback callback) {
        final Function function =
                new Function(
                        FUNC_UNREGISTERPARALLELFUNCTION,
                        Arrays.<Type>asList(
                                new org.fisco.bcos.sdk.abi.datatypes.Utf8String(functionName)),
                        Collections.<TypeReference<?>>emptyList());
        return asyncExecuteTransaction(function, callback);
    }

    public String getSignedTransactionForUnregisterParallelFunction(String functionName) {
        final Function function =
                new Function(
                        FUNC_UNREGISTERPARALLELFUNCTION,
                        Arrays.<Type>asList(
                                new org.fisco.bcos.sdk.abi.datatypes.Utf8String(functionName)),
                        Collections.<TypeReference<?>>emptyList());
        return createSignedTransaction(function);
    }

    public Tuple1<String> getUnregisterParallelFunctionInput(
            TransactionReceipt transactionReceipt) {
        String data = transactionReceipt.getInput().substring(10);
        final Function function =
                new Function(
                        FUNC_UNREGISTERPARALLELFUNCTION,
                        Arrays.<Type>asList(),
                        Arrays.<TypeReference<?>>asList(new TypeReference<Utf8String>() {}));
        List<Type> results = FunctionReturnDecoder.decode(data, function.getOutputParameters());
        return new Tuple1<String>((String) results.get(0).getValue());
    }

    public BigInteger getUserBalance(String username) throws ContractException {
        final Function function =
                new Function(
                        FUNC_GETUSERBALANCE,
                        Arrays.<Type>asList(
                                new org.fisco.bcos.sdk.abi.datatypes.Utf8String(username)),
                        Arrays.<TypeReference<?>>asList(new TypeReference<Uint256>() {}));
        return executeCallWithSingleValueReturn(function, BigInteger.class);
    }

    public TransactionReceipt buyEquipment(String username, BigInteger quantity) {
        final Function function =
                new Function(
                        FUNC_BUYEQUIPMENT,
                        Arrays.<Type>asList(
                                new org.fisco.bcos.sdk.abi.datatypes.Utf8String(username),
                                new org.fisco.bcos.sdk.abi.datatypes.generated.Uint256(quantity)),
                        Collections.<TypeReference<?>>emptyList());
        return executeTransaction(function);
    }

    public byte[] buyEquipment(String username, BigInteger quantity, TransactionCallback callback) {
        final Function function =
                new Function(
                        FUNC_BUYEQUIPMENT,
                        Arrays.<Type>asList(
                                new org.fisco.bcos.sdk.abi.datatypes.Utf8String(username),
                                new org.fisco.bcos.sdk.abi.datatypes.generated.Uint256(quantity)),
                        Collections.<TypeReference<?>>emptyList());
        return asyncExecuteTransaction(function, callback);
    }

    public String getSignedTransactionForBuyEquipment(String username, BigInteger quantity) {
        final Function function =
                new Function(
                        FUNC_BUYEQUIPMENT,
                        Arrays.<Type>asList(
                                new org.fisco.bcos.sdk.abi.datatypes.Utf8String(username),
                                new org.fisco.bcos.sdk.abi.datatypes.generated.Uint256(quantity)),
                        Collections.<TypeReference<?>>emptyList());
        return createSignedTransaction(function);
    }

    public Tuple2<String, BigInteger> getBuyEquipmentInput(TransactionReceipt transactionReceipt) {
        String data = transactionReceipt.getInput().substring(10);
        final Function function =
                new Function(
                        FUNC_BUYEQUIPMENT,
                        Arrays.<Type>asList(),
                        Arrays.<TypeReference<?>>asList(
                                new TypeReference<Utf8String>() {},
                                new TypeReference<Uint256>() {}));
        List<Type> results = FunctionReturnDecoder.decode(data, function.getOutputParameters());
        return new Tuple2<String, BigInteger>(
                (String) results.get(0).getValue(), (BigInteger) results.get(1).getValue());
    }

    public BigInteger getEquipmentInventory(String username) throws ContractException {
        final Function function =
                new Function(
                        FUNC_GETEQUIPMENTINVENTORY,
                        Arrays.<Type>asList(
                                new org.fisco.bcos.sdk.abi.datatypes.Utf8String(username)),
                        Arrays.<TypeReference<?>>asList(new TypeReference<Uint256>() {}));
        return executeCallWithSingleValueReturn(function, BigInteger.class);
    }

    public TransactionReceipt disableParallel() {
        final Function function =
                new Function(
                        FUNC_DISABLEPARALLEL,
                        Arrays.<Type>asList(),
                        Collections.<TypeReference<?>>emptyList());
        return executeTransaction(function);
    }

    public byte[] disableParallel(TransactionCallback callback) {
        final Function function =
                new Function(
                        FUNC_DISABLEPARALLEL,
                        Arrays.<Type>asList(),
                        Collections.<TypeReference<?>>emptyList());
        return asyncExecuteTransaction(function, callback);
    }

    public String getSignedTransactionForDisableParallel() {
        final Function function =
                new Function(
                        FUNC_DISABLEPARALLEL,
                        Arrays.<Type>asList(),
                        Collections.<TypeReference<?>>emptyList());
        return createSignedTransaction(function);
    }

    public TransactionReceipt enableParallel() {
        final Function function =
                new Function(
                        FUNC_ENABLEPARALLEL,
                        Arrays.<Type>asList(),
                        Collections.<TypeReference<?>>emptyList());
        return executeTransaction(function);
    }

    public byte[] enableParallel(TransactionCallback callback) {
        final Function function =
                new Function(
                        FUNC_ENABLEPARALLEL,
                        Arrays.<Type>asList(),
                        Collections.<TypeReference<?>>emptyList());
        return asyncExecuteTransaction(function, callback);
    }

    public String getSignedTransactionForEnableParallel() {
        final Function function =
                new Function(
                        FUNC_ENABLEPARALLEL,
                        Arrays.<Type>asList(),
                        Collections.<TypeReference<?>>emptyList());
        return createSignedTransaction(function);
    }

    public TransactionReceipt registerParallelFunction(
            String functionName, BigInteger criticalSize) {
        final Function function =
                new Function(
                        FUNC_REGISTERPARALLELFUNCTION,
                        Arrays.<Type>asList(
                                new org.fisco.bcos.sdk.abi.datatypes.Utf8String(functionName),
                                new org.fisco.bcos.sdk.abi.datatypes.generated.Uint256(
                                        criticalSize)),
                        Collections.<TypeReference<?>>emptyList());
        return executeTransaction(function);
    }

    public byte[] registerParallelFunction(
            String functionName, BigInteger criticalSize, TransactionCallback callback) {
        final Function function =
                new Function(
                        FUNC_REGISTERPARALLELFUNCTION,
                        Arrays.<Type>asList(
                                new org.fisco.bcos.sdk.abi.datatypes.Utf8String(functionName),
                                new org.fisco.bcos.sdk.abi.datatypes.generated.Uint256(
                                        criticalSize)),
                        Collections.<TypeReference<?>>emptyList());
        return asyncExecuteTransaction(function, callback);
    }

    public String getSignedTransactionForRegisterParallelFunction(
            String functionName, BigInteger criticalSize) {
        final Function function =
                new Function(
                        FUNC_REGISTERPARALLELFUNCTION,
                        Arrays.<Type>asList(
                                new org.fisco.bcos.sdk.abi.datatypes.Utf8String(functionName),
                                new org.fisco.bcos.sdk.abi.datatypes.generated.Uint256(
                                        criticalSize)),
                        Collections.<TypeReference<?>>emptyList());
        return createSignedTransaction(function);
    }

    public Tuple2<String, BigInteger> getRegisterParallelFunctionInput(
            TransactionReceipt transactionReceipt) {
        String data = transactionReceipt.getInput().substring(10);
        final Function function =
                new Function(
                        FUNC_REGISTERPARALLELFUNCTION,
                        Arrays.<Type>asList(),
                        Arrays.<TypeReference<?>>asList(
                                new TypeReference<Utf8String>() {},
                                new TypeReference<Uint256>() {}));
        List<Type> results = FunctionReturnDecoder.decode(data, function.getOutputParameters());
        return new Tuple2<String, BigInteger>(
                (String) results.get(0).getValue(), (BigInteger) results.get(1).getValue());
    }

    public BigInteger equipmentPrice() throws ContractException {
        final Function function =
                new Function(
                        FUNC_EQUIPMENTPRICE,
                        Arrays.<Type>asList(),
                        Arrays.<TypeReference<?>>asList(new TypeReference<Uint256>() {}));
        return executeCallWithSingleValueReturn(function, BigInteger.class);
    }

    public TransactionReceipt deposit(String username, BigInteger num) {
        final Function function =
                new Function(
                        FUNC_DEPOSIT,
                        Arrays.<Type>asList(
                                new org.fisco.bcos.sdk.abi.datatypes.Utf8String(username),
                                new org.fisco.bcos.sdk.abi.datatypes.generated.Uint256(num)),
                        Collections.<TypeReference<?>>emptyList());
        return executeTransaction(function);
    }

    public byte[] deposit(String username, BigInteger num, TransactionCallback callback) {
        final Function function =
                new Function(
                        FUNC_DEPOSIT,
                        Arrays.<Type>asList(
                                new org.fisco.bcos.sdk.abi.datatypes.Utf8String(username),
                                new org.fisco.bcos.sdk.abi.datatypes.generated.Uint256(num)),
                        Collections.<TypeReference<?>>emptyList());
        return asyncExecuteTransaction(function, callback);
    }

    public String getSignedTransactionForDeposit(String username, BigInteger num) {
        final Function function =
                new Function(
                        FUNC_DEPOSIT,
                        Arrays.<Type>asList(
                                new org.fisco.bcos.sdk.abi.datatypes.Utf8String(username),
                                new org.fisco.bcos.sdk.abi.datatypes.generated.Uint256(num)),
                        Collections.<TypeReference<?>>emptyList());
        return createSignedTransaction(function);
    }

    public Tuple2<String, BigInteger> getDepositInput(TransactionReceipt transactionReceipt) {
        String data = transactionReceipt.getInput().substring(10);
        final Function function =
                new Function(
                        FUNC_DEPOSIT,
                        Arrays.<Type>asList(),
                        Arrays.<TypeReference<?>>asList(
                                new TypeReference<Utf8String>() {},
                                new TypeReference<Uint256>() {}));
        List<Type> results = FunctionReturnDecoder.decode(data, function.getOutputParameters());
        return new Tuple2<String, BigInteger>(
                (String) results.get(0).getValue(), (BigInteger) results.get(1).getValue());
    }

    public static EquipmentPurchase load(
            String contractAddress, Client client, CryptoKeyPair credential) {
        return new EquipmentPurchase(contractAddress, client, credential);
    }

    public static EquipmentPurchase deploy(Client client, CryptoKeyPair credential)
            throws ContractException {
        return deploy(
                EquipmentPurchase.class,
                client,
                credential,
                getBinary(client.getCryptoSuite()),
                "");
    }
}
