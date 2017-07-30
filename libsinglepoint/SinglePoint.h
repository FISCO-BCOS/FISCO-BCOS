

#pragma once

#include <libethcore/SealEngine.h>



namespace dev
{

namespace eth
{

class SinglePoint: public SealEngineBase
{
public:
	SinglePoint();

	std::string name() const override { return "SinglePoint"; }
	unsigned revision() const override { return 1; }



	void verify(Strictness _s, BlockHeader const& _bi, BlockHeader const& _parent, bytesConstRef _block) const override;
	void verifyTransaction(ImportRequirements::value _ir, TransactionBase const& _t, BlockHeader const& _bi) const override;
	void populateFromParent(BlockHeader& _bi, BlockHeader const& _parent) const override;

	strings sealers() const override;

	void generateSeal(BlockHeader const& _bi, bytes const& _data) override;
	bool shouldSeal(Interface* _i) override;


	void setIntervalBlockTime(u256 _intervalBlockTime) override;

	u256 calculateDifficulty(BlockHeader const& _bi, BlockHeader const& _parent) const;
	u256 childGasLimit(BlockHeader const& _bi, u256 const& _gasFloorTarget = Invalid256) const;

	static void init();

private:
	bool verifySeal(BlockHeader const& _bi) const;
	bool quickVerifySeal(BlockHeader const& _bi) const;

	std::chrono::high_resolution_clock::time_point m_lasttimesubmit;

};

}
}
