#include "../../src/dag/TxDAG.h"
#include "../../src/dag/TxDAG2.h"
#include "../../src/dag/CriticalFields.h"
#include <vector>

using namespace std;
using namespace bcos;
using namespace bcos::executor;
using namespace bcos::executor::critical;
using namespace tbb::flow;

using Critical = vector<vector<string>>;

CriticalFieldsInterface::Ptr makeCriticals(int _totalTx) {
    Critical originMap = {{"111"}, {"222"}, {"333"}, {"444"}, {"555"}, {"666"}, {"777"}, {"888"},
                          {"999"}, {"101"}, {"102"}, {"103"}, {"104"}, {"105"}, {"106"}, {"107"},
                          {"108"}, {"109"}, {"120"}, {"121"}, {"122"}, {"123"}, {"124"}, {"125"}};
    CriticalFields<string>::Ptr criticals = make_shared<CriticalFields<string>>(_totalTx);
    for(int i = 0; i < _totalTx; i++){
        int rand1 = random() % originMap.size();
        int rand2 = random() % originMap.size();
        vector<vector<string>> critical = { {originMap[rand1]} , {originMap[rand2]} };
        criticals->put(i, make_shared<CriticalFields<string>::CriticalField>(std::move(critical)));
        /*
        stringstream ss;
        ss << i;
        res.push_back({string(ss.str())});
         */
    }
    return criticals;
}

void testTxDAG(CriticalFieldsInterface::Ptr criticals, shared_ptr<TxDAGInterface> _txDag, string name) {

    auto startTime = utcSteadyTime();
    cout << endl <<  name << " test start" << endl;
    _txDag->init(criticals,
        [&](ID id) {
            if (id % 100000 == 0)
            {
                std::cout << " [" << id << "] ";
            }
        });
    auto initTime = utcSteadyTime();
    try
    {
        _txDag->run(8);
    }
    catch (exception& e)
    {
        std::cout << "Exception" << boost::diagnostic_information(e) << std::endl;
    }
    auto endTime = utcSteadyTime();
    cout << endl << name << " cost(ms): initDAG=" << initTime - startTime << " run=" << endTime - initTime << " total=" << endTime - startTime <<  endl;
}

int main(int argc, const char* argv[])
{
    (void)argc;

    int _totalTx = stoi(argv[1]);

    auto criticals = makeCriticals(_totalTx);

    shared_ptr<TxDAGInterface> txDag = make_shared<TxDAG>();
    testTxDAG(criticals, txDag, "TxDAG");

    shared_ptr<TxDAGInterface> txDag2 = make_shared<TxDAG2>();
    testTxDAG(criticals, txDag2, "flowGraph");

    shared_ptr<TxDAGInterface> txDag3 = make_shared<TxDAG>();
    testTxDAG(criticals, txDag3, "TxDAG");

    shared_ptr<TxDAGInterface> txDag4 = make_shared<TxDAG2>();
    testTxDAG(criticals, txDag4, "flowGraph");

    return 0;
}