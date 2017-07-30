contract Testout {
    string data;
    function put() public constant returns (string ret) {
		ret = "hello";
    }
    function test() public constant returns (string _ret) {
        _ret = data;
    }
    function write(string _data) {
        data = _data;
    }

}
