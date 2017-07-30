contract Ok{
    
    struct Account{
        address account;
        uint balance;
    }
    
    struct  Translog {
        string time;
        address from;
        address to;
        uint amount;
    }
    
    Account from;
    Account to;
    
    Translog[] log;

    function Ok(){
        from.account=0x1;
        from.balance=10000000000;
        to.account=0x2;
        to.balance=0;

    }
    function get()constant returns(uint){
        return to.balance;
    }
    function trans(uint num){
    	from.balance=from.balance-num;
    	to.balance+=num;
    
    	log.push(Translog("20170413",from.account,to.account,num));
    }



}