/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file BlockChain.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include "BlockChain.h"

#if ETH_PROFILING_GPERF
#include <gperftools/profiler.h>
#endif

#include <boost/timer.hpp>
#include <boost/filesystem.hpp>
#include <json_spirit/JsonSpiritHeaders.h>

#include <libdevcore/Common.h>
#include <libdevcore/easylog.h>
#include <libdevcore/Assertions.h>
#include <libdevcore/RLP.h>
#include <libdevcore/TrieHash.h>
#include <libdevcore/FileSystem.h>
#include <libethcore/Exceptions.h>
#include <libethcore/BlockHeader.h>
#include <libethcore/CommonJS.h>
#include <libdevcore/easylog.h>
#include <libdiskencryption/BatchEncrypto.h>
#include <libweb3jsonrpc/RPCallback.h>
#include <UTXO/UTXOSharedData.h>

#include "Block.h"
#include "Defaults.h"
#include "GenesisInfo.h"
#include "NodeConnParamsManagerApi.h"
#include "State.h"
#include "Utility.h"

using namespace std;
using namespace dev;
using namespace dev::eth;
namespace js = json_spirit;
namespace fs = boost::filesystem;

#define ETH_CATCH 1
#define ETH_TIMED_IMPORTS 1



u256 BlockChain::maxBlockLimit = 1000;

std::ostream& dev::eth::operator<<(std::ostream& _out, BlockChain const& _bc)
{
	string cmp = toBigEndianString(_bc.currentHash());
	auto it = _bc.m_blocksDB->NewIterator(_bc.m_readOptions);
	for (it->SeekToFirst(); it->Valid(); it->Next())
		if (it->key().ToString() != "best")
		{
			try {
				BlockHeader d(bytesConstRef(it->value()));
				_out << toHex(it->key().ToString()) << ":   " << d.number() << " @ " << d.parentHash() << (cmp == it->key().ToString() ? "  BEST" : "") << "\n";
			}
			catch (...) {
				LOG(WARNING) << "Invalid DB entry:" << toHex(it->key().ToString()) << " -> " << toHex(bytesConstRef(it->value()));
			}
		}
	delete it;
	return _out;
}

ldb::Slice dev::eth::toSlice(h256 const& _h, unsigned _sub)
{
#if ALL_COMPILERS_ARE_CPP11_COMPLIANT
	static thread_local FixedHash<33> h = _h;
	h[32] = (uint8_t)_sub;
	return (ldb::Slice)h.ref();
#else
	static boost::thread_specific_ptr<FixedHash<33>> t_h;
	if (!t_h.get())
		t_h.reset(new FixedHash<33>);
	*t_h = FixedHash<33>(_h);
	(*t_h)[32] = (uint8_t)_sub;
	return (ldb::Slice)t_h->ref();
#endif //ALL_COMPILERS_ARE_CPP11_COMPLIANT
}

ldb::Slice dev::eth::toSlice(uint64_t _n, unsigned _sub)
{
#if ALL_COMPILERS_ARE_CPP11_COMPLIANT
	static thread_local FixedHash<33> h;
	toBigEndian(_n, bytesRef(h.data() + 24, 8));
	h[32] = (uint8_t)_sub;
	return (ldb::Slice)h.ref();
#else
	static boost::thread_specific_ptr<FixedHash<33>> t_h;
	if (!t_h.get())
		t_h.reset(new FixedHash<33>);
	bytesRef ref(t_h->data() + 24, 8);
	toBigEndian(_n, ref);
	(*t_h)[32] = (uint8_t)_sub;
	return (ldb::Slice)t_h->ref();
#endif
}

namespace dev
{
class WriteBatchNoter: public ldb::WriteBatch::Handler
{
	virtual void Put(ldb::Slice const& _key, ldb::Slice const& _value) { LOG(INFO) << "Put" << toHex(bytesConstRef(_key)) << "=>" << toHex(bytesConstRef(_value)); }
	virtual void Delete(ldb::Slice const& _key) { LOG(INFO) << "Delete" << toHex(bytesConstRef(_key)); }
};
}

#if ETH_DEBUG&&0
static const chrono::system_clock::duration c_collectionDuration = chrono::seconds(15);
static const unsigned c_collectionQueueSize = 2;
static const unsigned c_maxCacheSize = 1024 * 1024 * 1;
static const unsigned c_minCacheSize = 1;
#else

/// Duration between flushes.
static const chrono::system_clock::duration c_collectionDuration = chrono::seconds(60);

/// Length of death row (total time in cache is multiple of this and collection duration).
static const unsigned c_collectionQueueSize = 20;

/// Max size, above which we start forcing cache reduction.
static const unsigned c_maxCacheSize = 1024 * 1024 * 64;

/// Min size, below which we don't bother flushing it.
static const unsigned c_minCacheSize = 1024 * 1024 * 32;

#endif

BlockChain::BlockChain(Interface* _interface, ChainParams const& _p, std::string const& _dbPath, WithExisting _we, ProgressCallback const& _pc):
	m_dbPath(_dbPath),
	m_pnoncecheck(make_shared<NonceCheck>())
{
	init(_p, _dbPath);
	open(_dbPath, _we, _pc, _p.maxOpenFile, _p.writeBufferSize, _p.cacheSize);

	m_pnoncecheck->init(*this );

	m_interface = _interface;
}

BlockChain::~BlockChain()
{
	close();
}

bool BlockChain::isBlockLimitOk(Transaction const&_ts) const
{
	if ( (_ts.blockLimit()  == Invalid256) || ( number() >= _ts.blockLimit() ) || (_ts.blockLimit() > (number() + BlockChain::maxBlockLimit)  ) )
	{
		LOG(WARNING) << "BlockChain::isBlockLimitOk Fail! t.sha3()=" << _ts.sha3() << ",t.blockLimit=" << _ts.blockLimit() << ",number()=" << number() << ",maxBlockLimit=" << BlockChain::maxBlockLimit;
		return false;
	}

	return true;
}
bool BlockChain::isNonceOk(Transaction const&_ts, bool _needinsert) const
{
	if ( (_ts.randomid()  == Invalid256) || ( !m_pnoncecheck ) || (! m_pnoncecheck->ok(_ts, _needinsert) ) )
	{
		LOG(WARNING) << "BlockChain::isNonceOk Fail! t.sha3()=" << _ts.sha3() << ",t.nonce=" << _ts.randomid();
		return false;
	}

	return true;
}

u256 BlockChain::filterCheck(const Transaction & _t, FilterCheckScene _checkscene) const
{
	LOG(TRACE) << "BlockChain::filterCheck Scene：" << (int)(_checkscene);
	return m_interface->filterCheck(_t, _checkscene);
}

void    BlockChain::updateSystemContract(std::shared_ptr<Block> block)
{
	m_interface->updateSystemContract(block);
}

void BlockChain::updateCache(Address address) const {
	m_interface->updateCache(address);
}

BlockHeader const& BlockChain::genesis() const
{
	UpgradableGuard l(x_genesis);
	if (!m_genesis)
	{
		auto gb = m_params.genesisBlock();
		UpgradeGuard ul(l);
		m_genesis = BlockHeader(gb);
		m_genesisHeaderBytes = BlockHeader::extractHeader(&gb).data().toBytes();
		m_genesisHash = m_genesis.hash();
	}
	return m_genesis;
}

void BlockChain::init(ChainParams const& _p, std::string const& _path)
{
	// initialise deathrow.
	m_cacheUsage.resize(c_collectionQueueSize);
	m_lastCollection = chrono::system_clock::now();

	// Initialise with the genesis as the last block on the longest chain.
	m_params = _p;
	m_sealEngine.reset(m_params.createSealEngine());
	m_genesis.clear();
	genesis();

	// remove the next line real soon. we don't need to be supporting this forever.
	upgradeDatabase(_path, genesisHash());
}

unsigned BlockChain::open(std::string const& _path, WithExisting _we, int maxOpenFile, int writeBufferSize, int cacheSize)
{
	string path = _path.empty() ? Defaults::get()->m_dbPath : _path;
	string chainPath = path + "/" + toHex(m_genesisHash.ref().cropped(0, 4));
	string extrasPath = chainPath + "/" + toString(c_databaseVersion);

	fs::create_directories(extrasPath);
	DEV_IGNORE_EXCEPTIONS(fs::permissions(extrasPath, fs::owner_all));

	bytes status = contents(extrasPath + "/minor");
	unsigned lastMinor = c_minorProtocolVersion;
	if (!status.empty())
		DEV_IGNORE_EXCEPTIONS(lastMinor = (unsigned)RLP(status));
	if (c_minorProtocolVersion != lastMinor)
	{
		LOG(INFO) << "Killing extras database (DB minor version:" << lastMinor << " != our miner version: " << c_minorProtocolVersion << ").";
		DEV_IGNORE_EXCEPTIONS(boost::filesystem::remove_all(extrasPath + "/details.old"));
		boost::filesystem::rename(extrasPath + "/extras", extrasPath + "/extras.old");
		boost::filesystem::remove_all(extrasPath + "/state");
		writeFile(extrasPath + "/minor", rlp(c_minorProtocolVersion));
		lastMinor = (unsigned)RLP(status);
	}
	if (_we == WithExisting::Kill)
	{
		LOG(INFO) << "Killing blockchain & extras database (WithExisting::Kill).";
		boost::filesystem::remove_all(chainPath + "/blocks");
		boost::filesystem::remove_all(extrasPath + "/extras");
	}

	ldb::Options o;
	o.create_if_missing = true;
	//o.max_open_files = 256;
	o.max_open_files = maxOpenFile;
	//add by wheatli, for optimise
	//o.write_buffer_size = 100 * 1024 * 1024;
	o.write_buffer_size = writeBufferSize;
	//o.block_cache = ldb::NewLRUCache(256 * 1024 * 1024);
	o.block_cache = ldb::NewLRUCache(cacheSize);
	if (_we == WithExisting::Rescue) {
		ldb::Status blocksStatus = leveldb::RepairDB(chainPath + "/blocks", o);
		LOG(INFO) << "repair blocksDB:" << blocksStatus.ToString();
		ldb::Status extrasStatus = leveldb::RepairDB(extrasPath + "/extras", o);
		LOG(INFO) << "repair extrasDB:" << extrasStatus.ToString();
	}
#if ETH_ODBC

	LOG(INFO) << "state ethodbc is defined " << "\n";

	m_blocksDB = nullptr;
	m_extrasDB = nullptr;
	m_blocksDB = ldb::LvlDbInterfaceFactory::create(leveldb::DBUseType::blockType);
	if (m_blocksDB != nullptr)
	{
		LOG(INFO) << "block ethodbc is defined " << "\n";
	}
	else
	{
		LOG(INFO) << "block ethodbc is not defined " << "\n";
	}

	m_extrasDB = ldb::LvlDbInterfaceFactory::create(leveldb::DBUseType::extrasType);
	if (m_extrasDB != nullptr)
	{
		LOG(INFO) << "extras ethodbc is defined " << "\n";
	}
	else
	{
		LOG(INFO) << "extras ethodbc is not defined " << "\n";
	}
#else

	ldb::Status dbstatus = ldb::DB::Open(o, chainPath + "/blocks", &m_blocksDB);
	if(!dbstatus.ok()) {
		LOG(INFO) << "open m_blocksDB error:" << dbstatus.ToString();
	}

	if(m_blocksDB) {
		dbstatus = ldb::DB::Open(o, extrasPath + "/extras", &m_extrasDB);
		if(!dbstatus.ok()) {
			LOG(INFO) << "open m_extrasDB error:" << dbstatus.ToString();
		}
	}

#endif
//	m_writeOptions.sync = true;
	if (!m_blocksDB || !m_extrasDB)
	{
		if (boost::filesystem::space(chainPath + "/blocks").available < 1024)
		{
			LOG(ERROR) << "Not enough available space found on hard drive. Please free some up and then re-run. Bailing.";
			BOOST_THROW_EXCEPTION(NotEnoughAvailableSpace());
		}
		else
		{
			LOG(ERROR) <<
				  "Database " <<
				  (chainPath + "/blocks") <<
				  "or " <<
				  (extrasPath + "/extras") <<
				  " error";

			BOOST_THROW_EXCEPTION(DatabaseAlreadyOpen());
		}
	}
	if (_we != WithExisting::Verify && !details(m_genesisHash))
	{
		BlockHeader gb(m_params.genesisBlock());
		// Insert details of genesis block.
		m_details[m_genesisHash] = BlockDetails(0, gb.difficulty(), h256(), {});
		auto r = m_details[m_genesisHash].rlp();

		if (dev::getCryptoMod() != CRYPTO_DEFAULT)
		{
			bytes enData = encryptodata(r);
			m_extrasDB->Put(m_writeOptions, toSlice(m_genesisHash, ExtraDetails), (ldb::Slice)dev::ref(enData));
		}
		else
		{
			m_extrasDB->Put(m_writeOptions, toSlice(m_genesisHash, ExtraDetails), (ldb::Slice)dev::ref(r));
		}

		assert(isKnown(gb.hash()));
	}

#if ETH_PARANOIA
	checkConsistency();
#endif

	// TODO: Implement ability to rebuild details map from DB.
	std::string l;
	m_extrasDB->Get(m_readOptions, ldb::Slice("best"), &l);
	if (dev::getCryptoMod() != CRYPTO_DEFAULT && !l.empty())
	{
		bytes deData = decryptodata(l);
		l = asString(deData);
	}

	m_lastBlockHash = l.empty() ? m_genesisHash : *(h256*)l.data();
	m_lastBlockNumber = number(m_lastBlockHash);

	LOG(TRACE) << "Opened blockchain DB. Latest: " << currentHash() << (lastMinor == c_minorProtocolVersion ? "(rebuild not needed)" : "*** REBUILD NEEDED ***");
	return lastMinor;
}

void BlockChain::open(std::string const& _path, WithExisting _we, ProgressCallback const& _pc, int maxOpenFile, int writeBufferSize, int cacheSize)
{
	if (open(_path, _we, maxOpenFile, writeBufferSize, cacheSize) != c_minorProtocolVersion || _we == WithExisting::Verify)
		rebuild(_path, _pc);
}

void BlockChain::reopen(ChainParams const& _p, WithExisting _we, ProgressCallback const& _pc)
{
	close();
	init(_p, m_dbPath);
	open(m_dbPath, _we, _pc, _p.maxOpenFile, _p.writeBufferSize, _p.cacheSize);
}

void BlockChain::close()
{
	LOG(TRACE) << "Closing blockchain DB";
	// Not thread safe...
	delete m_extrasDB;
	delete m_blocksDB;
	m_lastBlockHash = m_genesisHash;
	m_lastBlockNumber = 0;
	m_details.clear();
	m_blocks.clear();
	m_logBlooms.clear();
	m_receipts.clear();
	m_transactionAddresses.clear();
	m_blockHashes.clear();
	m_blocksBlooms.clear();
	m_cacheUsage.clear();
	m_inUse.clear();
	m_lastLastHashes.clear();
}

void BlockChain::rebuild(std::string const& _path, std::function<void(unsigned, unsigned)> const& _progress)
{
	string path = _path.empty() ? Defaults::get()->m_dbPath : _path;
	string chainPath = path + "/" + toHex(m_genesisHash.ref().cropped(0, 4));
	string extrasPath = chainPath + "/" + toString(c_databaseVersion);

#if ETH_PROFILING_GPERF
	ProfilerStart("BlockChain_rebuild.log");
#endif

	unsigned originalNumber = m_lastBlockNumber;

	///////////////////////////////
	// TODO
	// - KILL ALL STATE/CHAIN
	// - REINSERT ALL BLOCKS
	///////////////////////////////

	// Keep extras DB around, but under a temp name
	delete m_extrasDB;
	m_extrasDB = nullptr;
	boost::filesystem::rename(extrasPath + "/extras", extrasPath + "/extras.old");
	ldb::DB* oldExtrasDB;

	ldb::Options o;
	o.create_if_missing = true;

	ldb::Status status = ldb::DB::Open(o, extrasPath + "/extras.old", &oldExtrasDB);
	LOG(INFO) << "open oldExtrasDB result:" << status.ToString();
	status = ldb::DB::Open(o, extrasPath + "/extras", &m_extrasDB);
	LOG(INFO) << "reopen m_extrasDB result:" << status.ToString();

	// Open a fresh state DB
	Block s = genesisBlock(State::openDB(path, m_genesisHash, WithExisting::Kill, 256, 64 * 1024 * 1024, 256 * 1024 * 1024));

	// Clear all memos ready for replay.
	m_details.clear();
	m_logBlooms.clear();
	m_receipts.clear();
	m_transactionAddresses.clear();
	m_blockHashes.clear();
	m_blocksBlooms.clear();
	m_lastLastHashes.clear();
	m_lastBlockHash = genesisHash();
	m_lastBlockNumber = 0;

	m_details[m_lastBlockHash].totalDifficulty = s.info().difficulty();

	auto r = m_details[m_lastBlockHash].rlp();
	if (dev::getCryptoMod() != CRYPTO_DEFAULT)
	{
		bytes enData = encryptodata(m_details[m_lastBlockHash].rlp());
		m_extrasDB->Put(m_writeOptions, toSlice(m_lastBlockHash, ExtraDetails), (ldb::Slice)dev::ref(enData));
	}
	else
	{
		m_extrasDB->Put(m_writeOptions, toSlice(m_lastBlockHash, ExtraDetails), (ldb::Slice)dev::ref(m_details[m_lastBlockHash].rlp()));
	}

	h256 lastHash = m_lastBlockHash;
	Timer t;
	for (unsigned d = 1; d <= originalNumber; ++d)
	{
		if (!(d % 1000))
		{
			LOG(ERROR) << "\n1000 blocks in " << t.elapsed() << "s = " << (1000.0 / t.elapsed()) << "b/s" << "\n";
			t.restart();
		}
		try
		{
			bytes b = block(queryExtras<BlockHash, uint64_t, ExtraBlockHash>(d, m_blockHashes, x_blockHashes, NullBlockHash, oldExtrasDB).value);

			BlockHeader bi(&b);

			if (bi.parentHash() != lastHash)
			{
				LOG(WARNING) << "DISJOINT CHAIN DETECTED; " << bi.hash() << "#" << d << " -> parent is" << bi.parentHash() << "; expected" << lastHash << "#" << (d - 1);
				return;
			}
			lastHash = bi.hash();
			import(b, s.db(), 0);
		}
		catch (...)
		{
			// Failed to import - stop here.
			break;
		}

		if (_progress)
			_progress(d, originalNumber);
	}

#if ETH_PROFILING_GPERF
	ProfilerStop();
#endif
	delete oldExtrasDB;
	boost::filesystem::remove_all(path + "/extras.old");
}

string BlockChain::dumpDatabase() const
{
	stringstream ss;

	ss << m_lastBlockHash << "\n";
	ldb::Iterator* i = m_extrasDB->NewIterator(m_readOptions);
	for (i->SeekToFirst(); i->Valid(); i->Next())
		ss << toHex(bytesConstRef(i->key())) << "/" << toHex(bytesConstRef(i->value())) << "\n";
	return ss.str();
}

LastHashes BlockChain::lastHashes(h256 const& _parent) const
{
	Guard l(x_lastLastHashes);
	if (m_lastLastHashes.empty() || m_lastLastHashes.front() != _parent)
	{
		m_lastLastHashes.resize(256);
		m_lastLastHashes[0] = _parent;
		for (unsigned i = 0; i < 255; ++i)
			m_lastLastHashes[i + 1] = m_lastLastHashes[i] ? info(m_lastLastHashes[i]).parentHash() : h256();
	}
	return m_lastLastHashes;
}

void BlockChain::addBlockCache(Block block, u256 td) const {
	DEV_WRITE_GUARDED(x_blockcache)
	{
		if ( m_blockCache.size() > kBlockCacheSize ) {
			// m_blockCache.clear();
			auto first_hash = m_blockCacheFIFO.front();
			m_blockCacheFIFO.pop_front();
			m_blockCache.erase(first_hash);
			// in case something unexcept error
			if (m_blockCache.size() > kBlockCacheSize) {  // meet error, cache and cacheFIFO not sync, clear the cache
				m_blockCache.clear();
				m_blockCacheFIFO.clear();
			}
		}

		m_blockCache.insert(std::make_pair(block.info().hash(), std::make_pair(block, td)));
		m_blockCacheFIFO.push_back(block.info().hash());  // add hashindex to the blockCache queue, use to remove first element when the cache is full
	}

}

std::pair<Block, u256> BlockChain::getBlockCache(h256 const& hash) const {
	DEV_READ_GUARDED(x_blockcache)
	{
		auto it = m_blockCache.find(hash);
		if (it == m_blockCache.end()) {
			return std::make_pair(Block(0), 0);
		}

		return it->second;
	}
	return std::make_pair(Block(0), 0); // just for passing compile
}

tuple<ImportRoute, bool, unsigned> BlockChain::sync(BlockQueue& _bq, OverlayDB const& _stateDB, unsigned _max)
{
//	_bq.tick(*this);

	VerifiedBlocks blocks;
	_bq.drain(blocks, _max);

	h256s fresh;
	h256s dead;
	h256s badBlocks;
	Transactions goodTransactions;
	unsigned count = 0;

	uint64_t startimport = 0;
	uint64_t endimport = 0;

	for (VerifiedBlock const& block : blocks)
	{
		do {
			try
			{
				// Nonce & uncle nonces already verified in verification thread at this point.
				ImportRoute r;
				DEV_TIMED_ABOVE("BLOCKSTAT timeout 500ms " + toString(block.verified.info.hash(WithoutSeal)) + toString(block.verified.info.number()), 500)
				//DEV_BLOCK_STAT_LOG(block.verified.info.hash(WithoutSeal), block.verified.info.number(), utcTime(), "BeforeImport");
				startimport = utcTime();
				r = import(block.verified, _stateDB, (ImportRequirements::Everything & ~ImportRequirements::ValidSeal & ~ImportRequirements::CheckUncles) != 0);
				endimport = utcTime();
				LOG(INFO) << block.verified.info.hash(WithoutSeal) << "," << block.verified.info.number() << ",trans" << r.goodTranactions.size() << " import timecost" << endimport - startimport << "ms";
				if ((endimport - startimport) > COnChainTimeLimit)
				{
					LOGCOMWARNING << WarningMap.at(OnChainTimeWarning) << "|blockNumber:" << block.verified.info.number() << " onChainTime:" << endimport - startimport << "ms";
				}
				fresh += r.liveBlocks;
				dead += r.deadBlocks;
				goodTransactions.reserve(goodTransactions.size() + r.goodTranactions.size());
				std::move(std::begin(r.goodTranactions), std::end(r.goodTranactions), std::back_inserter(goodTransactions));
				++count;
			}
			catch (dev::eth::UnknownParent)
			{
				LOG(WARNING) << "ODD: Import queue contains block with unknown parent.";// << LogTag::Error << boost::current_exception_diagnostic_information();
				// NOTE: don't reimport since the queue should guarantee everything in the right order.
				// Can't continue - chain bad.
				badBlocks.push_back(block.verified.info.hash());
			}
			catch (dev::eth::FutureTime)
			{
				LOG(WARNING) << "ODD: Import queue contains a block with future time.";
				this_thread::sleep_for(chrono::seconds(1));
				continue;
			}
			catch (dev::eth::TransientError)
			{
				this_thread::sleep_for(chrono::milliseconds(100));
				continue;
			}
			catch (dev::eth::AlreadyHaveBlock)
			{
				LOG(WARNING) << "ODD: Try to import one already have block. blk=" << block.verified.info.number() << ",hash=" << block.verified.info.hash(WithoutSeal);
				continue;
			}
			catch (Exception& ex)
			{
				LOG(ERROR) << "ODD: unknow error:" << diagnostic_information(ex);
//				LOG(INFO) << "Exception while importing block. Someone (Jeff? That you?) seems to be giving us dodgy blocks!";// << LogTag::Error << diagnostic_information(ex);
				if (m_onBad)
					m_onBad(ex);
				// NOTE: don't reimport since the queue should guarantee everything in the right order.
				// Can't continue - chain  bad.
				badBlocks.push_back(block.verified.info.hash());
			}
		} while (false);
	}
	return make_tuple(ImportRoute{dead, fresh, goodTransactions}, _bq.doneDrain(badBlocks), count);
}

pair<ImportResult, ImportRoute> BlockChain::attemptImport(bytes const& _block, OverlayDB const& _stateDB, bool _mustBeNew) noexcept
{
	try
	{
		return make_pair(ImportResult::Success, import(verifyBlock(&_block, m_onBad, ImportRequirements::OutOfOrderChecks), _stateDB, _mustBeNew));
	}
	catch (UnknownParent&)
	{
		return make_pair(ImportResult::UnknownParent, ImportRoute());
	}
	catch (AlreadyHaveBlock&)
	{
		return make_pair(ImportResult::AlreadyKnown, ImportRoute());
	}
	catch (FutureTime&)
	{
		return make_pair(ImportResult::FutureTimeKnown, ImportRoute());
	}
	catch (Exception& ex)
	{
		if (m_onBad)
			m_onBad(ex);
		return make_pair(ImportResult::Malformed, ImportRoute());
	}
}

ImportRoute BlockChain::import(bytes const& _block, OverlayDB const& _db, bool _mustBeNew)
{
	// VERIFY: populates from the block and checks the block is internally coherent.
	VerifiedBlockRef block;

#if ETH_CATCH
	try
#endif
	{
		block = verifyBlock(&_block, m_onBad, ImportRequirements::OutOfOrderChecks);
	}
#if ETH_CATCH
	catch (Exception& ex)
	{
		LOG(WARNING) << "   Malformed block: " << diagnostic_information(ex);
		ex << errinfo_phase(2);
		ex << errinfo_now(time(0));
		throw;
	}
#endif

	return import(block, _db, _mustBeNew);
}

void BlockChain::insert(bytes const& _block, bytesConstRef _receipts, bool _mustBeNew)
{
	// VERIFY: populates from the block and checks the block is internally coherent.
	VerifiedBlockRef block;

#if ETH_CATCH
	try
#endif
	{
		block = verifyBlock(&_block, m_onBad, ImportRequirements::OutOfOrderChecks);
	}
#if ETH_CATCH
	catch (Exception& ex)
	{
		LOG(WARNING) << "   Malformed block: " << diagnostic_information(ex);
		ex << errinfo_phase(2);
		ex << errinfo_now(time(0));
		throw;
	}
#endif

	insert(block, _receipts, _mustBeNew);
}

void BlockChain::insert(VerifiedBlockRef _block, bytesConstRef _receipts, bool _mustBeNew)
{
	// Check block doesn't already exist first!
	if (isKnown(_block.info.hash()) && _mustBeNew)
	{
		LOG(INFO) << _block.info.hash() << ": Not new.";
		BOOST_THROW_EXCEPTION(AlreadyHaveBlock());
	}

	// Work out its number as the parent's number + 1
	if (!isKnown(_block.info.parentHash(), false))
	{
		LOG(INFO) << _block.info.hash() << ": Unknown parent " << _block.info.parentHash();
		// We don't know the parent (yet) - discard for now. It'll get resent to us if we find out about its ancestry later on.
		BOOST_THROW_EXCEPTION(UnknownParent());
	}

	// Check receipts
	vector<bytesConstRef> receipts;
	for (auto i : RLP(_receipts))
		receipts.push_back(i.data());
	h256 receiptsRoot = orderedTrieRoot(receipts);
	if (_block.info.receiptsRoot() != receiptsRoot)
	{
		LOG(INFO) << _block.info.hash() << ": Invalid receipts root " << _block.info.receiptsRoot() << " not " << receiptsRoot;
		// We don't know the parent (yet) - discard for now. It'll get resent to us if we find out about its ancestry later on.
		BOOST_THROW_EXCEPTION(InvalidReceiptsStateRoot());
	}

	auto pd = details(_block.info.parentHash());
	if (!pd)
	{
		auto pdata = pd.rlp();
		LOG(INFO) << "Details is returning false despite block known:" << RLP(pdata);
		auto parentBlock = block(_block.info.parentHash());
		LOG(INFO) << "isKnown:" << isKnown(_block.info.parentHash());
		LOG(INFO) << "last/number:" << m_lastBlockNumber << m_lastBlockHash << _block.info.number();
		LOG(INFO) << "Block:" << BlockHeader(&parentBlock);
		LOG(INFO) << "RLP:" << RLP(parentBlock);
		LOG(INFO) << "DATABASE CORRUPTION: CRITICAL FAILURE";
		exit(-1);
	}

	// Check it's not crazy
	if (_block.info.timestamp() > utcTime() && !m_params.otherParams.count("allowFutureBlocks"))
	{
		LOG(INFO) << _block.info.hash() << ": Future time " << _block.info.timestamp() << " (now at " << utcTime() << ")";
		// Block has a timestamp in the future. This is no good.
		BOOST_THROW_EXCEPTION(FutureTime());
	}

	verifyBlock(_block.block, m_onBad, ImportRequirements::InOrderChecks | ImportRequirements::CheckMinerSignatures);

	// OK - we're happy. Insert into database.
	BatchEncrypto blocksBatch;
	BatchEncrypto extrasBatch;

	BlockLogBlooms blb;
	for (auto i : RLP(_receipts))
		blb.blooms.push_back(TransactionReceipt(i.data()).bloom());

	// ensure parent is cached for later addition.
	// TODO: this is a bit horrible would be better refactored into an enveloping UpgradableGuard
	// together with an "ensureCachedWithUpdatableLock(l)" method.
	// This is safe in practice since the caches don't get flushed nearly often enough to be
	// done here.
	details(_block.info.parentHash());
	DEV_WRITE_GUARDED(x_details)
	{
		if (!dev::contains(m_details[_block.info.parentHash()].children, _block.info.hash()))
			m_details[_block.info.parentHash()].children.push_back(_block.info.hash());
	}

	blocksBatch.Put(toSlice(_block.info.hash()), ldb::Slice(_block.block));
	DEV_READ_GUARDED(x_details)
	extrasBatch.Put(toSlice(_block.info.parentHash(), ExtraDetails), (ldb::Slice)dev::ref(m_details[_block.info.parentHash()].rlp()));

	BlockDetails bd((unsigned)pd.number + 1, pd.totalDifficulty + _block.info.difficulty(), _block.info.parentHash(), {});
	extrasBatch.Put(toSlice(_block.info.hash(), ExtraDetails), (ldb::Slice)dev::ref(bd.rlp()));
	extrasBatch.Put(toSlice(_block.info.hash(), ExtraLogBlooms), (ldb::Slice)dev::ref(blb.rlp()));
	extrasBatch.Put(toSlice(_block.info.hash(), ExtraReceipts), (ldb::Slice)_receipts);

	ldb::Status o = m_blocksDB->Write(m_writeOptions, &blocksBatch);
	if (!o.ok())
	{
		LOG(ERROR) << "Error writing to blockchain database: " << o.ToString();
		WriteBatchNoter n;
		blocksBatch.Iterate(&n);
		LOG(ERROR) << "Fail writing to blockchain database. Bombing out.";
		exit(-1);
	}

	o = m_extrasDB->Write(m_writeOptions, &extrasBatch);
	if (!o.ok())
	{
		LOG(ERROR) << "Error writing to extras database: " << o.ToString();
		WriteBatchNoter n;
		extrasBatch.Iterate(&n);
		LOG(ERROR) << "Fail writing to extras database. Bombing out.";
		exit(-1);
	}
}

void BlockChain::checkBlockValid(h256 const& _hash, bytes const& _block, Block & _outBlock) const {
	VerifiedBlockRef block = verifyBlock(&_block, m_onBad, ImportRequirements::Everything);

	if (_hash != block.info.hash()) {
		LOG(WARNING) << "hash error, " << block.info.hash() << "," << _hash;
		BOOST_THROW_EXCEPTION(HashError());
	}

	// 禁止叔块
	if (block.info.number() <= info().number()) {
		LOG(WARNING) << "height error, h=" << block.info.number() << ", curr=" << info().number();
		BOOST_THROW_EXCEPTION(HeightError());
	}

	if (isKnown(block.info.hash())) {
		LOG(WARNING) << block.info.hash() << ": Not new.";
		BOOST_THROW_EXCEPTION(AlreadyHaveBlock());
	}

	if (!isKnown(block.info.parentHash(), false) || !details(block.info.parentHash())) {
		LOG(WARNING) << block.info.hash() << ": Unknown parent " << block.info.parentHash();
		// We don't know the parent (yet) - discard for now. It'll get resent to us if we find out about its ancestry later on.
		BOOST_THROW_EXCEPTION(UnknownParent() << errinfo_hash256(block.info.parentHash()));
	}

	// Check it's not crazy
	if (block.info.timestamp() > utcTime() && !m_params.otherParams.count("allowFutureBlocks"))
	{
		LOG(WARNING) << block.info.hash() << ": Future time " << block.info.timestamp() << " (now at " << utcTime() << ")";
		// Block has a timestamp in the future. This is no good.
		BOOST_THROW_EXCEPTION(FutureTime());
	}

	std::map<std::string, NodeConnParams> all_node;
	NodeConnManagerSingleton::GetInstance().getAllNodeConnInfo(static_cast<int>(block.info.number() - 1), all_node);
	unsigned miner_num = 0;
	for (auto iter = all_node.begin(); iter != all_node.end(); ++iter) {
		if (iter->second._iIdentityType == EN_ACCOUNT_TYPE_MINER) {
			++miner_num;
		}
	}
	h512s miner_list;
	miner_list.resize(miner_num);
	for (auto iter = all_node.begin(); iter != all_node.end(); ++iter) {
		if (iter->second._iIdentityType == EN_ACCOUNT_TYPE_MINER) {
			auto idx = static_cast<unsigned>(iter->second._iIdx);

			if (idx >= miner_num) {
				LOG(WARNING) << "idx out of bound, idx=" << idx << ",miner_num=" << miner_num;
				BOOST_THROW_EXCEPTION(MinerListError());
			}

			miner_list[idx] = jsToPublic(toJS(iter->second._sNodeId));

		}
	}
	if (miner_list != block.info.nodeList()) {
		LOG(WARNING) << "miner list error, " << _hash;
		ostringstream oss;
		for (size_t i = 0; i < miner_list.size(); ++i) {
			oss << miner_list[i] << ",";
		}
		LOG(WARNING) << "get miner_list size=" << miner_list.size() << ",value=" << oss.str();
		ostringstream oss2;
		for (size_t i = 0; i < block.info.nodeList().size(); ++i) {
			oss2 << block.info.nodeList()[i] << ",";
		}
		LOG(WARNING) << "block node_list size=" << block.info.nodeList().size() << ",value=" << oss2.str();
		BOOST_THROW_EXCEPTION(MinerListError());
	}

	//Block s(*this, _db);
	//s.enactOn(block, *this);
	
	_outBlock.setEvmCoverLog(m_params.evmCoverLog);
	_outBlock.setEvmEventLog(m_params.evmEventLog);
	_outBlock.enactOn(block, *this, false);
}


ImportRoute BlockChain::import(VerifiedBlockRef const& _block, OverlayDB const& _db, bool _mustBeNew)
{
	//@tidy This is a behemoth of a method - could do to be split into a few smaller ones.
#if ETH_TIMED_IMPORTS
	Timer total;
	double preliminaryChecks;
	double enactment;
	double collation;
	double writing;
	double checkBest;
	Timer t;
#endif

	// Check block doesn't already exist first!
	if (isKnown(_block.info.hash()) && _mustBeNew)
	{
		LOG(INFO) << _block.info.hash() << ": Not new.";
		BOOST_THROW_EXCEPTION(AlreadyHaveBlock() << errinfo_block(_block.block.toBytes()));
	}

	// Work out its number as the parent's number + 1
	if (!isKnown(_block.info.parentHash(), false))	// doesn't have to be current.
	{
		LOG(INFO) << _block.info.hash() << ": Unknown parent " << _block.info.parentHash();
		// We don't know the parent (yet) - discard for now. It'll get resent to us if we find out about its ancestry later on.
		BOOST_THROW_EXCEPTION(UnknownParent() << errinfo_hash256(_block.info.parentHash()));
	}

	auto pd = details(_block.info.parentHash());
	if (!pd)
	{
		auto pdata = pd.rlp();
		LOG(INFO) << "Details is returning false despite block known:" << RLP(pdata);
		auto parentBlock = block(_block.info.parentHash());
		LOG(INFO) << "isKnown:" << isKnown(_block.info.parentHash());
		LOG(INFO) << "last/number:" << m_lastBlockNumber << m_lastBlockHash << _block.info.number();
		LOG(INFO) << "Block:" << BlockHeader(&parentBlock);
		LOG(INFO) << "RLP:" << RLP(parentBlock);
		LOG(INFO) << "DATABASE CORRUPTION: CRITICAL FAILURE";
		exit(-1);
	}

	// Check it's not crazy
	if (_block.info.timestamp() > utcTime() && !m_params.otherParams.count("allowFutureBlocks"))
	{
		LOG(INFO) << _block.info.hash() << ": Future time " << _block.info.timestamp() << " (now at " << utcTime() << ")";
		// Block has a timestamp in the future. This is no good.
		BOOST_THROW_EXCEPTION(FutureTime());
	}

	// Verify parent-critical parts
	verifyBlock(_block.block, m_onBad, ImportRequirements::InOrderChecks | ImportRequirements::CheckMinerSignatures);

	LOG(INFO) << "Attempting import of " << _block.info.hash() << "...";



#if ETH_TIMED_IMPORTS
	preliminaryChecks = t.elapsed();
	t.restart();
#endif
	BatchEncrypto blocksBatch;
	BatchEncrypto extrasBatch;

	h256 newLastBlockHash = currentHash();
	unsigned newLastBlockNumber = number();

	BlockLogBlooms blb;
	BlockReceipts br;

	u256 td;
	Transactions goodTransactions;

	std::shared_ptr<Block> tempBlock(new Block(*this, _db));

#if ETH_CATCH
	try
#endif
	{
		
		u256  tdIncrease = 0;

		auto pair = getBlockCache(_block.info.hash());
		
		if (pair.second != 0) {
			tdIncrease = pair.second;
			*tempBlock = pair.first;

			
		}
		else {
			tempBlock->setEvmCoverLog(m_params.evmCoverLog);
			tempBlock->setEvmEventLog(m_params.evmEventLog);
			tdIncrease = tempBlock->enactOn(_block, *this);
			addBlockCache(*tempBlock, tdIncrease);
		}

		
		for (unsigned i = 0; i < tempBlock->pending().size(); ++i) {
			blb.blooms.push_back(tempBlock->receipt(i).bloom());
			br.receipts.push_back(tempBlock->receipt(i));
			goodTransactions.push_back(tempBlock->pending()[i]);

			LOG(INFO) << " Hash=" << (tempBlock->pending()[i].sha3()) << ",Randid=" << tempBlock->pending()[i].randomid() << ",Packed=" << utcTime();

		}

		tempBlock->commitAll();


		td = pd.totalDifficulty + tdIncrease;

#if ETH_TIMED_IMPORTS
		enactment = t.elapsed();
		t.restart();
#endif // ETH_TIMED_IMPORTS

#if ETH_PARANOIA
		checkConsistency();
#endif // ETH_PARANOIA

		// All ok - insert into DB

		// ensure parent is cached for later addition.
		// TODO: this is a bit horrible would be better refactored into an enveloping UpgradableGuard
		// together with an "ensureCachedWithUpdatableLock(l)" method.
		// This is safe in practice since the caches don't get flushed nearly often enough to be
		// done here.
		details(_block.info.parentHash());
		DEV_WRITE_GUARDED(x_details)
		m_details[_block.info.parentHash()].children.push_back(_block.info.hash());

#if ETH_TIMED_IMPORTS
		collation = t.elapsed();
		t.restart();
#endif // ETH_TIMED_IMPORTS

		
		blocksBatch.Put(toSlice(_block.info.hash()), ldb::Slice(_block.block));//_block.block [0]=head [1]=transactionlist [2]=unclelist [3]=hash [4]=siglist
		DEV_READ_GUARDED(x_details)
		extrasBatch.Put(toSlice(_block.info.parentHash(), ExtraDetails), (ldb::Slice)dev::ref(m_details[_block.info.parentHash()].rlp()));

		extrasBatch.Put(toSlice(_block.info.hash(), ExtraDetails), (ldb::Slice)dev::ref(BlockDetails((unsigned)pd.number + 1, td, _block.info.parentHash(), {}).rlp()));
		extrasBatch.Put(toSlice(_block.info.hash(), ExtraLogBlooms), (ldb::Slice)dev::ref(blb.rlp()));
		extrasBatch.Put(toSlice(_block.info.hash(), ExtraReceipts), (ldb::Slice)dev::ref(br.rlp()));

#if ETH_TIMED_IMPORTS
		writing = t.elapsed();
		t.restart();
#endif // ETH_TIMED_IMPORTS
	}

#if ETH_CATCH
	catch (BadRoot& ex)
	{
		m_pnoncecheck->delCache(goodTransactions);
		LOG(WARNING) << "*** BadRoot error! Trying to import" << _block.info.hash() << "needed root" << ex.root;
		LOG(WARNING) << _block.info;
		// Attempt in import later.
		BOOST_THROW_EXCEPTION(TransientError());
	}
	catch ( BcFilterCheckError & ex)
	{
		LOG(WARNING) << "*** Bad Filter Check error! Trying to import" << _block.info.hash() ;

		ex << errinfo_now(time(0));
		ex << errinfo_block(_block.block.toBytes());
		if (!_block.info.extraData().empty())
			ex << errinfo_extraData(_block.info.extraData());
		throw;
	}
	catch ( BcNoDeployPermissionError &ex)
	{
		LOG(WARNING) << "*** Bad NoDeployPermissionError error! Trying to import" << _block.info.hash() ;

		ex << errinfo_now(time(0));
		ex << errinfo_block(_block.block.toBytes());
		if (!_block.info.extraData().empty())
			ex << errinfo_extraData(_block.info.extraData());
		throw;
	}
	catch ( FilterCheckFail const& ex)
	{
		LOG(WARNING) << "*** Bad BlockLimit error! Trying to import" << _block.info.hash() ;
		ex << errinfo_now(time(0));
		ex << errinfo_block(_block.block.toBytes());
		if (!_block.info.extraData().empty())
			ex << errinfo_extraData(_block.info.extraData());
		throw;
	}
	catch ( BcBlockLimitCheckError & ex )
	{
		LOG(WARNING) << "*** Bad BlockLimit error! Trying to import" << _block.info.hash() ;
		//if( goodTransactions.size() )
		//LOG(WARNING)<<"  "<goodTransactions[goodTransactions.size()-1].sha3();

		ex << errinfo_now(time(0));
		ex << errinfo_block(_block.block.toBytes());
		if (!_block.info.extraData().empty())
			ex << errinfo_extraData(_block.info.extraData());
		throw;
	}
	catch ( BcNonceCheckError & ex)
	{
		m_pnoncecheck->delCache(goodTransactions);

		LOG(WARNING) << "*** Bad Nonce error! Trying to import" << _block.info.hash() ;
		//if( goodTransactions.size() )
		//LOG(WARNING)<<"  "<goodTransactions[goodTransactions.size()-1].sha3();

		ex << errinfo_now(time(0));
		ex << errinfo_block(_block.block.toBytes());
		if (!_block.info.extraData().empty())
			ex << errinfo_extraData(_block.info.extraData());
		throw;
	}
	catch (Exception& ex)
	{

		LOG(ERROR) << boost::current_exception_diagnostic_information() << "\n";
		m_pnoncecheck->delCache(goodTransactions);
		ex << errinfo_now(time(0));
		ex << errinfo_block(_block.block.toBytes());
		// only populate extraData if we actually managed to extract it. otherwise,
		// we might be clobbering the existing one.
		if (!_block.info.extraData().empty())
			ex << errinfo_extraData(_block.info.extraData());
		throw;
	}
#endif // ETH_CATCH

	bool isunclechain = false; 

	h256s route;
	h256 common;
	bool isImportedAndBest = false;
	// This might be the new best block...
	h256 last = currentHash();
	if (td > details(last).totalDifficulty || (m_sealEngine->chainParams().tieBreakingGas && td == details(last).totalDifficulty && _block.info.gasUsed() > info(last).gasUsed()))
	{
		// don't include bi.hash() in treeRoute, since it's not yet in details DB...
		// just tack it on afterwards.
		unsigned commonIndex;
		tie(route, common, commonIndex) = treeRoute(last, _block.info.parentHash());
		route.push_back(_block.info.hash());

		// Most of the time these two will be equal - only when we're doing a chain revert will they not be
		if (common != last)
		{
			clearCachesDuringChainReversion(number(common) + 1);
			isunclechain = true;
		}


		// Go through ret backwards (i.e. from new head to common) until hash != last.parent and
		// update m_transactionAddresses, m_blockHashes
		for (auto i = route.rbegin(); i != route.rend() && *i != common; ++i)
		{
			BlockHeader tbi;
			if (*i == _block.info.hash())
				tbi = _block.info;
			else
				tbi = BlockHeader(block(*i));

			// Collate logs into blooms.
			h256s alteredBlooms;
			{
				LogBloom blockBloom = tbi.logBloom();
				blockBloom.shiftBloom<3>(sha3(tbi.author().ref()));

				// Pre-memoize everything we need before locking x_blocksBlooms
				for (unsigned level = 0, index = (unsigned)tbi.number(); level < c_bloomIndexLevels; level++, index /= c_bloomIndexSize)
					blocksBlooms(chunkId(level, index / c_bloomIndexSize));

				WriteGuard l(x_blocksBlooms);
				for (unsigned level = 0, index = (unsigned)tbi.number(); level < c_bloomIndexLevels; level++, index /= c_bloomIndexSize)
				{
					unsigned i = index / c_bloomIndexSize;
					unsigned o = index % c_bloomIndexSize;
					alteredBlooms.push_back(chunkId(level, i));
					m_blocksBlooms[alteredBlooms.back()].blooms[o] |= blockBloom;
				}
			}
			// Collate transaction hashes and remember who they were.
			//h256s newTransactionAddresses;
			{
				bytes blockBytes;
				RLP blockRLP(*i == _block.info.hash() ? _block.block : & (blockBytes = block(*i)));
				TransactionAddress ta;
				ta.blockHash = tbi.hash();
				for (ta.index = 0; ta.index < blockRLP[1].itemCount(); ++ta.index)
					extrasBatch.Put(toSlice(sha3(blockRLP[1][ta.index].data()), ExtraTransactionAddress), (ldb::Slice)dev::ref(ta.rlp()));
				
			}

			// Update database with them.
			ReadGuard l1(x_blocksBlooms);
			for (auto const& h : alteredBlooms)
				extrasBatch.Put(toSlice(h, ExtraBlocksBlooms), (ldb::Slice)dev::ref(m_blocksBlooms[h].rlp()));
			extrasBatch.Put(toSlice(h256(tbi.number()), ExtraBlockHash), (ldb::Slice)dev::ref(BlockHash(tbi.hash()).rlp()));
		}

		// FINALLY! change our best hash.
		{
			newLastBlockHash = _block.info.hash();
			newLastBlockNumber = (unsigned)_block.info.number();
			isImportedAndBest = true;
		}

		LOG(INFO) << "   Imported and best" << td << " (#" << _block.info.number() << "). Has" << (details(_block.info.parentHash()).children.size() - 1) << "siblings. Route:" << route << " transcations size=" << goodTransactions.size() << ",genIndex=" << _block.info.genIndex();
		if (_block.info.gasUsed() > ((BlockHeader::maxBlockHeadGas * CGasUsedLimit) / 100))
		{
			LOGCOMWARNING << WarningMap.at(GasUsedWarning) << "|blockNumber:" << _block.info.number() << " gasUsed:" << _block.info.gasUsed();
		}
	}
	else
	{
		LOG(WARNING) << "   Imported but not best (oTD:" << details(last).totalDifficulty << " > TD:" << td << "; " << details(last).number << ".." << _block.info.number() << ")";
	}

	ldb::Status o = m_blocksDB->Write(m_writeOptions, &blocksBatch);
	if (!o.ok())
	{
		LOG(WARNING) << "Error writing to blockchain database: " << o.ToString();
		WriteBatchNoter n;
		blocksBatch.Iterate(&n);
		LOG(WARNING) << "Fail writing to blockchain database. Bombing out.";
		exit(-1);
	}

	o = m_extrasDB->Write(m_writeOptions, &extrasBatch);
	if (!o.ok())
	{
		LOG(WARNING) << "Error writing to extras database: " << o.ToString();
		WriteBatchNoter n;
		extrasBatch.Iterate(&n);
		LOG(WARNING) << "Fail writing to extras database. Bombing out.";
		exit(-1);
	}

#if ETH_PARANOIA
	if (isKnown(_block.info.hash()) && !details(_block.info.hash()))
	{
		LOG(ERROR) << "Known block just inserted has no details.";
		LOG(ERROR) << "Block:" << _block.info;
		LOG(ERROR) << "DATABASE CORRUPTION: CRITICAL FAILURE";
		exit(-1);
	}

	try
	{
		State canary(_db, BaseState::Empty);
		canary.populateFromChain(*this, _block.info.hash());
	}
	catch (...)
	{
		LOG(ERROR) << "Failed to initialise State object form imported block.";
		LOG(ERROR) << "Block:" << _block.info;
		LOG(ERROR) << "DATABASE CORRUPTION: CRITICAL FAILURE";
		exit(-1);
	}
#endif // ETH_PARANOIA

	m_pnoncecheck->delCache(goodTransactions);

	if (m_lastBlockHash != newLastBlockHash)

	{
		DEV_WRITE_GUARDED(x_lastBlockHash)
		{
			m_lastBlockHash = newLastBlockHash;
			m_lastBlockNumber = newLastBlockNumber;
			
			if (dev::getCryptoMod() != CRYPTO_DEFAULT)
			{
				bytes enData = encryptodata(ldb::Slice((char const *)m_lastBlockHash.data(), 32));
				o = m_extrasDB->Put(m_writeOptions, ldb::Slice("best"), ldb::Slice((char const*)enData.data(), enData.size()));
			}
			else
			{
				o = m_extrasDB->Put(m_writeOptions, ldb::Slice("best"), ldb::Slice((char const*)&m_lastBlockHash, 32));
			}
			if (!o.ok())
			{
				LOG(ERROR) << "Error writing to extras database: " << o.ToString();
				//cout << "Put" << toHex(bytesConstRef(ldb::Slice("best"))) << "=>" << toHex(bytesConstRef(ldb::Slice((char const*)&m_lastBlockHash, 32)));
				LOG(ERROR) << "Fail writing to extras database. Bombing out.";
				exit(-1);
			}
		}


		m_pnoncecheck->updateCache(*this, isunclechain); 
		
		//this->updateSystemContract(goodTransactions);
		this->updateSystemContract(tempBlock);
		LOG(TRACE) << "BlockChain setBlockInfo";
		UTXOModel::UTXOSharedData::getInstance()->setPreBlockInfo(tempBlock, lastHashes());
		CallbackWorker::getInstance().addBlock(tempBlock);
	}

#if ETH_PARANOIA
	checkConsistency();
#endif // ETH_PARANOIA

#if ETH_TIMED_IMPORTS
	checkBest = t.elapsed();
	if (total.elapsed() > 0.5)
	{
		unsigned const gasPerSecond = static_cast<double>(_block.info.gasUsed()) / enactment;
		LOG(WARNING) << "SLOW IMPORT: "
		      << "{ \"blockHash\": \"" << _block.info.hash() << "\", "
		      << "\"blockNumber\": " << _block.info.number() << ", "
		      << "\"importTime\": " << total.elapsed() << ", "
		      << "\"gasPerSecond\": " << gasPerSecond << ", "
		      << "\"preliminaryChecks\":" << preliminaryChecks << ", "
		      << "\"enactment\":" << enactment << ", "
		      << "\"collation\":" << collation << ", "
		      << "\"writing\":" << writing << ", "
		      << "\"checkBest\":" << checkBest << ", "
		      << "\"transactions\":" << _block.transactions.size() << ", "
		      << "\"gasUsed\":" << _block.info.gasUsed() << " }";
	}
#endif // ETH_TIMED_IMPORTS

	if (!route.empty())
		noteCanonChanged();

	if (isImportedAndBest && m_onBlockImport)
		m_onBlockImport(_block.info);

	h256s fresh;
	h256s dead;
	bool isOld = true;
	for (auto const& h : route)
		if (h == common)
			isOld = false;
		else if (isOld)
			dead.push_back(h);
		else
			fresh.push_back(h);
	return ImportRoute{dead, fresh, move(goodTransactions)};
}

void BlockChain::clearBlockBlooms(unsigned _begin, unsigned _end)
{
	//   ... c c c c c c c c c c C o o o o o o
	//   ...                               /=15        /=21
	// L0...| ' | ' | ' | ' | ' | ' | ' | 'b|x'x|x'x|x'e| /=11
	// L1...|   '   |   '   |   '   |   ' b | x ' x | x ' e |   /=6
	// L2...|       '       |       '   b   |   x   '   x   |   e   /=3
	// L3...|               '       b       |       x       '       e
	// model: c_bloomIndexLevels = 4, c_bloomIndexSize = 2

	//   ...                               /=15        /=21
	// L0...| ' ' ' | ' ' ' | ' ' ' | ' ' 'b|x'x'x'x|x'e' ' |
	// L1...|       '       '       '   b   |   x   '   x   '   e   '       |
	// L2...|               b               '               x               '                e              '                               |
	// model: c_bloomIndexLevels = 2, c_bloomIndexSize = 4

	// algorithm doesn't have the best memoisation coherence, but eh well...

	unsigned beginDirty = _begin;
	unsigned endDirty = _end;
	for (unsigned level = 0; level < c_bloomIndexLevels; level++, beginDirty /= c_bloomIndexSize, endDirty = (endDirty - 1) / c_bloomIndexSize + 1)
	{
		// compute earliest & latest index for each level, rebuild from previous levels.
		for (unsigned item = beginDirty; item != endDirty; ++item)
		{
			unsigned bunch = item / c_bloomIndexSize;
			unsigned offset = item % c_bloomIndexSize;
			auto id = chunkId(level, bunch);
			LogBloom acc;
			if (!!level)
			{
				// rebuild the bloom from the previous (lower) level (if there is one).
				auto lowerChunkId = chunkId(level - 1, item);
				for (auto const& bloom : blocksBlooms(lowerChunkId).blooms)
					acc |= bloom;
			}
			blocksBlooms(id);	// make sure it has been memoized.
			m_blocksBlooms[id].blooms[offset] = acc;
		}
	}
}

void BlockChain::rescue(OverlayDB const& _db)
{
	cout << "Rescuing database..." << "\n";

	unsigned u = 1;
	while (true)
	{
		try {
			if (isKnown(numberHash(u)))
				u *= 2;
			else
				break;
		}
		catch (...)
		{
			break;
		}
	}
	unsigned l = u / 2;
	cout << "Finding last likely block number..." << "\n";
	while (u - l > 1)
	{
		unsigned m = (u + l) / 2;
		cout << " " << m << flush;
		if (isKnown(numberHash(m)))
			l = m;
		else
			u = m;
	}
	cout << "  lowest is " << l << "\n";
	for (; l > 0; --l)
	{
		h256 h = numberHash(l);
		cout << "Checking validity of " << l << " (" << h << ")..." << flush;
		try
		{
			cout << "block..." << flush;
			BlockHeader bi(block(h));
			cout << "extras..." << flush;
			details(h);
			cout << "state..." << flush;
			if (_db.exists(bi.stateRoot()))
				break;
		}
		catch (...) {}
	}
	cout << "OK." << "\n";
	rewind(l);
}

void BlockChain::rewind(unsigned _newHead)
{
	DEV_WRITE_GUARDED(x_lastBlockHash)
	{
		if (_newHead >= m_lastBlockNumber)
			return;
		clearCachesDuringChainReversion(_newHead + 1);
		m_lastBlockHash = numberHash(_newHead);
		m_lastBlockNumber = _newHead;

		ldb::Status o;
		if (dev::getCryptoMod() != CRYPTO_DEFAULT)
		{
			bytes enData = encryptodata(ldb::Slice((const char*)m_lastBlockHash.data(), 32));
			o = m_extrasDB->Put(m_writeOptions, ldb::Slice("best"), (ldb::Slice)dev::ref(enData));
		}
		else
		{
			o = m_extrasDB->Put(m_writeOptions, ldb::Slice("best"), ldb::Slice((char const*)&m_lastBlockHash, 32));
		}

		if (!o.ok())
		{
			LOG(ERROR) << "Error writing to extras database: " << o.ToString();
			LOG(ERROR) << "Put" << toHex(bytesConstRef(ldb::Slice("best"))) << "=>" << toHex(bytesConstRef(ldb::Slice((char const*)&m_lastBlockHash, 32)));
			LOG(ERROR) << "Fail writing to extras database. Bombing out.";
			exit(-1);
		}
		noteCanonChanged();
	}
}

tuple<h256s, h256, unsigned> BlockChain::treeRoute(h256 const& _from, h256 const& _to, bool _common, bool _pre, bool _post) const
{
//	LOG(DEBUG) << "treeRoute" << _from << "..." << _to;
	if (!_from || !_to)
		return make_tuple(h256s(), h256(), 0);
	h256s ret;
	h256s back;
	unsigned fn = details(_from).number;
	unsigned tn = details(_to).number;
//	LOG(DEBUG) << "treeRoute" << fn << "..." << tn;
	h256 from = _from;
	while (fn > tn)
	{
		if (_pre)
			ret.push_back(from);
		from = details(from).parent;
		fn--;
//		LOG(DEBUG) << "from:" << fn << _from;
	}
	h256 to = _to;
	while (fn < tn)
	{
		if (_post)
			back.push_back(to);
		to = details(to).parent;
		tn--;
//		LOG(DEBUG) << "to:" << tn << _to;
	}
	for (;; from = details(from).parent, to = details(to).parent)
	{
		if (_pre && (from != to || _common))
			ret.push_back(from);
		if (_post && (from != to || (!_pre && _common)))
			back.push_back(to);
		fn--;
		tn--;
//		LOG(DEBUG) << "from:" << fn << _from << "; to:" << tn << _to;
		if (from == to)
			break;
		if (!from)
			assert(from);
		if (!to)
			assert(to);
	}
	ret.reserve(ret.size() + back.size());
	unsigned i = ret.size() - (int)(_common && !ret.empty() && !back.empty());
	for (auto it = back.rbegin(); it != back.rend(); ++it)
		ret.push_back(*it);
	return make_tuple(ret, from, i);
}

void BlockChain::noteUsed(h256 const& _h, unsigned _extra) const
{
	auto id = CacheID(_h, _extra);
	Guard l(x_cacheUsage);
	m_cacheUsage[0].insert(id);
	if (m_cacheUsage[1].count(id))
		m_cacheUsage[1].erase(id);
	else
		m_inUse.insert(id);
}

template <class K, class T> static unsigned getHashSize(unordered_map<K, T> const& _map)
{
	unsigned ret = 0;
	for (auto const& i : _map)
		ret += i.second.size + 64;
	return ret;
}

void BlockChain::updateStats() const
{
	m_lastStats.memBlocks = 0;
	DEV_READ_GUARDED(x_blocks)
	for (auto const& i : m_blocks)
		m_lastStats.memBlocks += i.second.size() + 64;
	DEV_READ_GUARDED(x_details)
	m_lastStats.memDetails = getHashSize(m_details);
	DEV_READ_GUARDED(x_logBlooms)
	DEV_READ_GUARDED(x_blocksBlooms)
	m_lastStats.memLogBlooms = getHashSize(m_logBlooms) + getHashSize(m_blocksBlooms);
	DEV_READ_GUARDED(x_receipts)
	m_lastStats.memReceipts = getHashSize(m_receipts);
	DEV_READ_GUARDED(x_blockHashes)
	m_lastStats.memBlockHashes = getHashSize(m_blockHashes);
	DEV_READ_GUARDED(x_transactionAddresses)
	m_lastStats.memTransactionAddresses = getHashSize(m_transactionAddresses);
}

void BlockChain::garbageCollect(bool _force)
{
	updateStats();

	if (!_force && chrono::system_clock::now() < m_lastCollection + c_collectionDuration && m_lastStats.memTotal() < c_maxCacheSize)
		return;
	if (m_lastStats.memTotal() < c_minCacheSize)
		return;

	m_lastCollection = chrono::system_clock::now();

	Guard l(x_cacheUsage);
	WriteGuard l1(x_blocks);
	WriteGuard l2(x_details);
	WriteGuard l3(x_blockHashes);
	WriteGuard l4(x_receipts);
	WriteGuard l5(x_logBlooms);
	WriteGuard l6(x_transactionAddresses);
	WriteGuard l7(x_blocksBlooms);
	for (CacheID const& id : m_cacheUsage.back())
	{
		m_inUse.erase(id);
		// kill i from cache.
		switch (id.second)
		{
		case (unsigned)-1:
			m_blocks.erase(id.first);
			break;
		case ExtraDetails:
			m_details.erase(id.first);
			break;
		case ExtraReceipts:
			m_receipts.erase(id.first);
			break;
		case ExtraLogBlooms:
			m_logBlooms.erase(id.first);
			break;
		case ExtraTransactionAddress:
			m_transactionAddresses.erase(id.first);
			break;
		case ExtraBlocksBlooms:
			m_blocksBlooms.erase(id.first);
			break;
		}
	}
	m_cacheUsage.pop_back();
	m_cacheUsage.push_front(std::unordered_set<CacheID> {});
}

void BlockChain::checkConsistency()
{
	DEV_WRITE_GUARDED(x_details)
	m_details.clear();
	ldb::Iterator* it = m_blocksDB->NewIterator(m_readOptions);
	for (it->SeekToFirst(); it->Valid(); it->Next())
		if (it->key().size() == 32)
		{
			h256 h((byte const*)it->key().data(), h256::ConstructFromPointer);
			auto dh = details(h);
			auto p = dh.parent;
			if (p != h256() && p != m_genesisHash)	// TODO: for some reason the genesis details with the children get squished. not sure why.
			{
				auto dp = details(p);
				if (asserts(contains(dp.children, h)))
					LOG(INFO) << "Apparently the database is corrupt. Not much we can do at this stage...";
				if (assertsEqual(dp.number, dh.number - 1))
					LOG(INFO) << "Apparently the database is corrupt. Not much we can do at this stage...";
			}
		}
	delete it;
}

void BlockChain::clearCachesDuringChainReversion(unsigned _firstInvalid)
{
	unsigned end = number() + 1;
	DEV_WRITE_GUARDED(x_blockHashes)
	for (auto i = _firstInvalid; i < end; ++i)
		m_blockHashes.erase(i);
	DEV_WRITE_GUARDED(x_transactionAddresses)
	m_transactionAddresses.clear();	// TODO: could perhaps delete them individually?

	// If we are reverting previous blocks, we need to clear their blooms (in particular, to
	// rebuild any higher level blooms that they contributed to).
	clearBlockBlooms(_firstInvalid, end);
}

static inline unsigned upow(unsigned a, unsigned b) { if (!b) return 1; while (--b > 0) a *= a; return a; }
static inline unsigned ceilDiv(unsigned n, unsigned d) { return (n + d - 1) / d; }
//static inline unsigned floorDivPow(unsigned n, unsigned a, unsigned b) { return n / upow(a, b); }
//static inline unsigned ceilDivPow(unsigned n, unsigned a, unsigned b) { return ceilDiv(n, upow(a, b)); }

// Level 1
// [xxx.            ]

// Level 0
// [.x............F.]
// [........x.......]
// [T.............x.]
// [............    ]

// F = 14. T = 32

vector<unsigned> BlockChain::withBlockBloom(LogBloom const& _b, unsigned _earliest, unsigned _latest) const
{
	vector<unsigned> ret;

	// start from the top-level
	unsigned u = upow(c_bloomIndexSize, c_bloomIndexLevels);

	// run through each of the top-level blockbloom blocks
	for (unsigned index = _earliest / u; index <= ceilDiv(_latest, u); ++index)				// 0
		ret += withBlockBloom(_b, _earliest, _latest, c_bloomIndexLevels - 1, index);

	return ret;
}

vector<unsigned> BlockChain::withBlockBloom(LogBloom const& _b, unsigned _earliest, unsigned _latest, unsigned _level, unsigned _index) const
{
	// 14, 32, 1, 0
	// 14, 32, 0, 0
	// 14, 32, 0, 1
	// 14, 32, 0, 2

	vector<unsigned> ret;

	unsigned uCourse = upow(c_bloomIndexSize, _level + 1);
	// 256
	// 16
	unsigned uFine = upow(c_bloomIndexSize, _level);
	// 16
	// 1

	unsigned obegin = _index == _earliest / uCourse ? _earliest / uFine % c_bloomIndexSize : 0;
	// 0
	// 14
	// 0
	// 0
	unsigned oend = _index == _latest / uCourse ? (_latest / uFine) % c_bloomIndexSize + 1 : c_bloomIndexSize;
	// 3
	// 16
	// 16
	// 1

	BlocksBlooms bb = blocksBlooms(_level, _index);
	for (unsigned o = obegin; o < oend; ++o)
		if (bb.blooms[o].contains(_b))
		{
			// This level has something like what we want.
			if (_level > 0)
				ret += withBlockBloom(_b, _earliest, _latest, _level - 1, o + _index * c_bloomIndexSize);
			else
				ret.push_back(o + _index * c_bloomIndexSize);
		}
	return ret;
}

h256Hash BlockChain::allKinFrom(h256 const& _parent, unsigned _generations) const
{
	// Get all uncles cited given a parent (i.e. featured as uncles/main in parent, parent + 1, ... parent + 5).
	h256 p = _parent;
	h256Hash ret = { p };
	// p and (details(p).parent: i == 5) is likely to be overkill, but can't hurt to be cautious.
	for (unsigned i = 0; i < _generations && p != m_genesisHash; ++i, p = details(p).parent)
	{
		ret.insert(details(p).parent);
		auto b = block(p);
		for (auto i : RLP(b)[2])
			ret.insert(sha3(i.data()));
	}
	return ret;
}

bool BlockChain::isKnown(h256 const& _hash, bool _isCurrent) const
{
	if (_hash == m_genesisHash)
		return true;

	DEV_READ_GUARDED(x_blocks)
	if (!m_blocks.count(_hash))
	{
		string d;
		m_blocksDB->Get(m_readOptions, toSlice(_hash), &d);
		if (d.empty())
			return false;
	}
	DEV_READ_GUARDED(x_details)
	if (!m_details.count(_hash))
	{
		string d;
		m_extrasDB->Get(m_readOptions, toSlice(_hash, ExtraDetails), &d);
		if (d.empty())
			return false;
	}
//	return true;
	return !_isCurrent || details(_hash).number <= m_lastBlockNumber;		// to allow rewind functionality.
}

bytes BlockChain::block(h256 const& _hash) const
{
	if (_hash == m_genesisHash)
		return m_params.genesisBlock();

	{
		ReadGuard l(x_blocks);
		auto it = m_blocks.find(_hash);
		if (it != m_blocks.end())
			return it->second;
	}

	//LOG(TRACE)<<"BlockChain::block"<<_hash;

	string d;
	m_blocksDB->Get(m_readOptions, toSlice(_hash), &d);

	if (d.empty())
	{
		LOG(WARNING) << "Couldn't find requested block:" << _hash;
		return bytes();
	}
	if (dev::getCryptoMod() != CRYPTO_DEFAULT && !d.empty())
	{
		bytes deData = decryptodata(d);
		d = asString(deData);
	}

	noteUsed(_hash);

	WriteGuard l(x_blocks);
	m_blocks[_hash].resize(d.size());
	memcpy(m_blocks[_hash].data(), d.data(), d.size());

	return m_blocks[_hash];
}

bytes BlockChain::headerData(h256 const& _hash) const
{
	if (_hash == m_genesisHash)
		return m_genesisHeaderBytes;

	{
		ReadGuard l(x_blocks);
		auto it = m_blocks.find(_hash);
		if (it != m_blocks.end())
			return BlockHeader::extractHeader(&it->second).data().toBytes();
	}

	string d;
	m_blocksDB->Get(m_readOptions, toSlice(_hash), &d);

	if (d.empty())
	{
		LOG(WARNING) << "Couldn't find requested block:" << _hash;
		return bytes();
	}
	if (dev::getCryptoMod() != CRYPTO_DEFAULT && !d.empty())
	{
		bytes deData = decryptodata(d);
		d = asString(deData);
	}

	noteUsed(_hash);
	WriteGuard l(x_blocks);
	m_blocks[_hash].resize(d.size());
	memcpy(m_blocks[_hash].data(), d.data(), d.size());

	return BlockHeader::extractHeader(&m_blocks[_hash]).data().toBytes();
}

Block BlockChain::genesisBlock(OverlayDB const& _db) const
{
	h256 r = BlockHeader(m_params.genesisBlock()).stateRoot();
	Block ret(*this, _db, BaseState::Empty);
	if (!_db.exists(r))
	{
		ret.noteChain(*this);
		dev::eth::commit(m_params.genesisState, ret.mutableState().m_state);		// bit horrible. maybe consider a better way of constructing it?
		ret.mutableState().db().commit();											// have to use this db() since it's the one that has been altered with the above commit.
		if (ret.mutableState().rootHash() != r)
		{
			LOG(WARNING) << "Hinted genesis block's state root hash is incorrect!";
			LOG(WARNING) << "Hinted" << r << ", computed" << ret.mutableState().rootHash();
			// TODO: maybe try to fix it by altering the m_params's genesis block?
			exit(-1);
		}
	}
	ret.m_previousBlock = BlockHeader(m_params.genesisBlock());
	ret.resetCurrent();
	return ret;
}


bytes BlockChain::encryptodata(std::string const& v)
{
	map<int, string> m_keyData = getDataKey();
	string dataKey = m_keyData[0] + m_keyData[1] + m_keyData[2] + m_keyData[3];
	string ivData = dataKey.substr(0, 16);
	bytes enData = aesCBCEncrypt(bytesConstRef(&v), dataKey, dataKey.length(), bytesConstRef{(const unsigned char*)ivData.c_str(), ivData.length()});
	return enData;
}


bytes BlockChain::encryptodata(bytesConstRef const& v)
{
	map<int, string> m_keyData = getDataKey();
	string dataKey = m_keyData[0] + m_keyData[1] + m_keyData[2] + m_keyData[3];
	string ivData = dataKey.substr(0, 16);
	bytes enData = aesCBCEncrypt(v, dataKey, dataKey.length(), bytesConstRef{(const unsigned char*)ivData.c_str(), ivData.length()});
	return enData;
}


bytes BlockChain::encryptodata(bytes const& v)
{
	map<int, string> m_keyData = getDataKey();
	string dataKey = m_keyData[0] + m_keyData[1] + m_keyData[2] + m_keyData[3];
	string ivData = dataKey.substr(0, 16);
	bytes enData = aesCBCEncrypt(bytesConstRef(&v), dataKey, dataKey.length(), bytesConstRef{(const unsigned char*)ivData.c_str(), ivData.length()});
	return enData;
}

bytes BlockChain::decryptodata(std::string const& v) const
{
	map<int, string> m_keyData = getDataKey();
	string dataKey = m_keyData[0] + m_keyData[1] + m_keyData[2] + m_keyData[3];
	string ivData = dataKey.substr(0, 16);
	bytes deData = aesCBCDecrypt(bytesConstRef{(const unsigned char*)v.c_str(), v.length()}, dataKey, dataKey.length(), bytesConstRef{(const unsigned char*)ivData.c_str(), ivData.length()});
	return deData;
}


VerifiedBlockRef BlockChain::verifyBlock(bytesConstRef _block, std::function<void(Exception&)> const& _onBad, ImportRequirements::value _ir) const
{
	VerifiedBlockRef res;
	BlockHeader h;
	try
	{
		h = BlockHeader(_block);
		if (!!(_ir & ImportRequirements::PostGenesis) && (!h.parentHash() || h.number() == 0))
			BOOST_THROW_EXCEPTION(InvalidParentHash() << errinfo_required_h256(h.parentHash()) << errinfo_currentNumber(h.number()));

		BlockHeader parent;
		if (!!(_ir & ImportRequirements::Parent))
		{
			bytes parentHeader(headerData(h.parentHash()));
			if (parentHeader.empty())
				BOOST_THROW_EXCEPTION(InvalidParentHash() << errinfo_required_h256(h.parentHash()) << errinfo_currentNumber(h.number()));
			parent = BlockHeader(parentHeader, HeaderData, h.parentHash());
		}
		sealEngine()->verify((_ir & ImportRequirements::ValidSeal) ? Strictness::CheckEverything : Strictness::QuickNonce, h, parent, _block);
		res.info = h;
	}
	catch (Exception& ex)
	{
		ex << errinfo_phase(1);
		ex << errinfo_now(time(0));
		ex << errinfo_block(_block.toBytes());
		// only populate extraData if we actually managed to extract it. otherwise,
		// we might be clobbering the existing one.
		if (!h.extraData().empty())
			ex << errinfo_extraData(h.extraData());
		if (_onBad)
			_onBad(ex);
		throw;
	}

	RLP r(_block);
	unsigned i = 0;
	if (_ir & (ImportRequirements::UncleBasic | ImportRequirements::UncleParent | ImportRequirements::UncleSeals))
		for (auto const& uncle : r[2])
		{
			BlockHeader uh(uncle.data(), HeaderData);
			try
			{
				BlockHeader parent;
				if (!!(_ir & ImportRequirements::UncleParent))
				{
					bytes parentHeader(headerData(uh.parentHash()));
					if (parentHeader.empty())
						BOOST_THROW_EXCEPTION(InvalidUncleParentHash() << errinfo_required_h256(uh.parentHash()) << errinfo_currentNumber(h.number()) << errinfo_uncleNumber(uh.number()));
					parent = BlockHeader(parentHeader, HeaderData, uh.parentHash());
				}
				sealEngine()->verify((_ir & ImportRequirements::UncleSeals) ? Strictness::CheckEverything : Strictness::IgnoreSeal, uh, parent);
			}
			catch (Exception& ex)
			{
				ex << errinfo_phase(1);
				ex << errinfo_uncleIndex(i);
				ex << errinfo_now(time(0));
				ex << errinfo_block(_block.toBytes());
				// only populate extraData if we actually managed to extract it. otherwise,
				// we might be clobbering the existing one.
				if (!uh.extraData().empty())
					ex << errinfo_extraData(uh.extraData());
				if (_onBad)
					_onBad(ex);
				throw;
			}
			++i;
		}
	i = 0;
	if (_ir & (ImportRequirements::TransactionBasic | ImportRequirements::TransactionSignatures))
		for (RLP const& tr : r[1])
		{
			bytesConstRef d = tr.data();
			try
			{
				Transaction t(d, (_ir & ImportRequirements::TransactionSignatures) ? CheckTransaction::Everything : CheckTransaction::None);
				m_sealEngine->verifyTransaction(_ir, t, h);
				res.transactions.push_back(t);
			}
			catch (Exception& ex)
			{
				ex << errinfo_phase(1);
				ex << errinfo_transactionIndex(i);
				ex << errinfo_transaction(d.toBytes());
				ex << errinfo_block(_block.toBytes());
				// only populate extraData if we actually managed to extract it. otherwise,
				// we might be clobbering the existing one.
				if (!h.extraData().empty())
					ex << errinfo_extraData(h.extraData());
				if (_onBad)
					_onBad(ex);
				throw;
			}
			++i;
		}

	
	if (_ir & ImportRequirements::CheckMinerSignatures) {
		if (m_sign_checker && !m_sign_checker(h, r[4].toVector<std::pair<u256, Signature>>())) {
			LOG(WARNING) << "Error - check sign failed:" << h.number();
			BOOST_THROW_EXCEPTION(CheckSignFailed());
		}
	}

	res.block = bytesConstRef(_block);
	return res;
}
