# CRITICAL — Predictable, replayable oracle entropy: bettors pre-compute and selectively take the outcome of every Qubic game contract

**Audit date:** 2026-11-12
**Auditor:** Arena agent (blockchain security audit, no RPC used)
**Repo:** `qubic/core` — audited snapshot `8cd0dbfa559b2b7f5666853649ebe352b1a47578` (main, 2026-11-04)
**Still present at HEAD:** `9af0d56f2566da9b8156dae068a2e72109bd30ae` (main, 2026-11-11, `update params for epoch 239 / v1.312.0`)
`git log 8cd0dbfa..origin/main -- src/contracts/Random.h src/contracts/QDuel.h src/contracts/Qusino.h src/contracts/RandomLottery.h` → **empty**; the vulnerable lines are byte-identical (`git show origin/main:src/contracts/Random.h` → `:176 locals.entropyIdx = locals.stream * 10 + input.collateralTier; :177 locals.entropy = state.get().entropy.get(locals.entropyIdx); :202 state.mut().earnedAmount += ...`).

---

## 1. Summary

The `RANDOM` contract (index 3) publishes its per-tick commit/reveal entropy through a **public, fixed-price, read-only procedure** `BuyEntropy`. Reading it does **not** consume, rotate, mark-used or bind the value to the caller. Every game contract that settles a wager *inside the same invocation in which the player decides to participate* therefore settles on a value that any attacker can already know — exactly, byte-for-byte — and cheaply (256 bits = 25 600 quants = **0.0256 QU**).

Because participation is *optional and un-timed* for the second actor (the joiner / the bettor), the attacker does not need to beat the reveal race: they only act on the ticks whose fixed entropy favors them and skip the rest. Their downside is the 25 600-quant read fee; their upside is the whole pot. **Realised win rate = 100 %**, and the attack is a single transaction if the attacker is a smart contract (no timing at all).

Primary exploit target: **QDuel** (`QDUEL`, index 23, live). Same root cause affects **Qusino** (`QUSINO`, 26), **RandomLottery** (`RL`, 16), **Pulse** (`PULSE`, 24), **QThirtyFour**, **QRaffle** (19), **QTREAT** (30) — all of them call `RANDOM.BuyEntropy` (proof: grep below).

Class: *oracle mis-integration / predictable randomness at settlement time* (criterion 1: "oracle mis-integration", "state transitions").

---

## 2. The bug, in the code

### 2.1 `RANDOM::BuyEntropy` — the read is free-riding on shared state, and never advances it

`src/contracts/Random.h:165-221` (snapshot `8cd0dbfa`):

```c++
PUBLIC_PROCEDURE_WITH_LOCALS(BuyEntropy)
{
    locals.entropyCost = static_cast<sint64>(input.numberOfBits) * state.get().bitFee;

    if (input.collateralTier <= 9
        && input.numberOfBits >= 1 && input.numberOfBits <= 4096
        && qpi.invocationReward() >= locals.entropyCost)
    {
        // Offset +2: skip the current tick's slot (being overwritten) and read the last finalized stream.
        locals.stream     = mod<uint32>(qpi.tick() + 2, 3);
        locals.entropyIdx = locals.stream * 10 + input.collateralTier;
        locals.entropy    = state.get().entropy.get(locals.entropyIdx);      // (A) pure read
        ...
        if (locals.entropy == locals.zeroEntropy || !locals.trusteeOk)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
        }
        else
        {
            state.mut().earnedAmount += static_cast<uint64>(locals.entropyCost);   // (B) only accounting
            if (qpi.invocationReward() > locals.entropyCost)
            {
                qpi.transfer(qpi.invocator(), qpi.invocationReward() - locals.entropyCost);
            }
            for (locals.i = 0; locals.i < input.numberOfBits; locals.i++)
            {
                output.entropy.set(locals.i, locals.entropy.get(locals.i));        // (C) hand out the bits
            }
        }
    }
    ...
}
```

Missing validation / failing calculation:

* **(A)+(B)+(C)** the value at `entropy[stream*10 + tier]` is returned **without any state transition** — no cursor, no per-consumer offset, no nonce, no "already-drawn" flag, no burn. The only write is `earnedAmount += fee`. So the read is *replayable* (any number of times, by anyone) and *shared* (all consumers of the same `(tick, tier)` get the identical 4096-bit string).
* The index is a **public function of the tick only**: `(tick + 2) % 3 * 10 + tier`. There is nothing for the attacker to guess.
* `input.trustee` would restrict the provider set, but every consumer passes `id::zero()`, i.e. "any provider" — no binding either.
* `RevealAndCommit` explicitly refuses contract providers (`src/contracts/Random.h:227`: `if (qpi.isContractId(qpi.invocator())) { refund; return; }`) — **but `BuyEntropy` has no such restriction and, crucially, none of the consumer contracts check that the *player* is not a contract** (verified: `grep -n "isContractId" src/contracts/QDuel.h src/contracts/Qusino.h src/contracts/Pulse.h src/contracts/RandomLottery.h src/contracts/QThirtyFour.h` → no hits). A malicious contract can therefore read the oracle **and** play in the same invocation.

Registration proving the entry point is a public, permissionless user procedure — `src/contracts/Random.h:595-607`:

```c++
REGISTER_USER_PROCEDURE(RevealAndCommit, 1);
REGISTER_USER_PROCEDURE(BuyEntropy, 2);
INITIALIZE() { state.mut().bitFee = RANDOM_BITFEE; }   // RANDOM_BITFEE = 100 quants / bit  (Random.h:3)
```

### 2.2 QDuel settles the pot with exactly that value, in the joiner's own transaction

`src/contracts/QDuel.h:686-790` (`ConnectToRoom`, the entry point where the second player stakes their money and the duel is decided):

```c++
if (locals.requiredBalance <= RL_RANDOM_ENTROPY_FEE) { ...refund + close...; return; }

locals.buyEntropyInput.collateralTier = RL_RANDOM_COLLATERAL_TIER;   // 0
locals.buyEntropyInput.numberOfBits   = RL_RANDOM_ENTROPY_BITS;      // 256
locals.buyEntropyInput.trustee        = id::zero();                  // "any provider"
INVOKE_OTHER_CONTRACT_PROCEDURE(RANDOM, BuyEntropy, locals.buyEntropyInput,
                                locals.buyEntropyOutput, RL_RANDOM_ENTROPY_FEE);   // 100*256 = 25600
if (interContractCallError != NoCallError || RL::isZeroEntropy(locals.buyEntropyOutput.entropy)) { ...return; }
...
locals.amount = sadd(locals.room.amount, locals.room.amount);
locals.amount = locals.amount - RL_RANDOM_ENTROPY_FEE;
locals.getWinnerPlayer_input.player1 = locals.room.owner;
locals.getWinnerPlayer_input.player2 = qpi.invocator();
locals.getWinnerPlayer_input.entropy = locals.buyEntropyOutput.entropy;
CALL(GetWinnerPlayer, locals.getWinnerPlayer_input, locals.getWinnerPlayer_output);
```

`src/contracts/QDuel.h:1198-1215`:

```c++
PRIVATE_FUNCTION_WITH_LOCALS(GetWinnerPlayer)
{
    ...
    locals.randomData.entropy = input.entropy;
    locals.randomData.players = locals.players;
    locals.randomData.tick    = qpi.tick();
    locals.randomValue        = qpi.K12(locals.randomData);
    output.winner = locals.players.get(locals.randomValue.u64._0 & 1);   // ONE public bit decides the pot
}
```

Why the existing defenses do not stop it:

| Defense | Why it fails |
|---|---|
| commit/reveal scheme of `RANDOM` (providers commit `K12(reveal)`, reveal in `END_TICK`) | It protects against *provider* manipulation, not against *consumer-side* foresight. By the time a stream is finalized, its value is immutable and readable by anyone for a fixed price — and `BuyEntropy` deliberately reads "the last **finalized** stream" (`(tick+2)%3`). |
| `qpi.tick()` inside the K12 pre-image | The attacker knows the tick they will land in (they pay it into the same invocation; a contract reads and decides within one tick, so the pre-image is fully known). |
| `RL_RANDOM_ENTROPY_FEE` charged per read | Costs 25 600 quants; the pot it unlocks is `2 * room.amount`, and `CreateRoom` requires `stake >= 2 * RL_RANDOM_ENTROPY_FEE` (`QDuel.h:623`) — i.e. **every legal room is worth more than the read**. Break-even stake computed by the PoC: **76 800 quants (0.0768 QU)**. |
| `input.trustee` | Unused (`id::zero()`) by every consumer. |
| `minimumDuelAmount` = 10 000 | Irrelevant; the binding minimum is the 2×fee rule above. |

### 2.3 The other consumers of the same value (same root cause, same file pattern)

```
src/contracts/QDuel.h:737          INVOKE_OTHER_CONTRACT_PROCEDURE(RANDOM, BuyEntropy, ...)   # settles pot now
src/contracts/RandomLottery.h:648  INVOKE_OTHER_CONTRACT_PROCEDURE(RANDOM, BuyEntropy, ...)
src/contracts/Pulse.h:1796         INVOKE_OTHER_CONTRACT_PROCEDURE(RANDOM, BuyEntropy, ...)
src/contracts/QThirtyFour.h:1609   INVOKE_OTHER_CONTRACT_PROCEDURE_E(RANDOM, BuyEntropy, ...)
src/contracts/QRaffle.h:2307       INVOKE_OTHER_CONTRACT_PROCEDURE(RANDOM, BuyEntropy, ...)
src/contracts/QTREAT.h:1807        INVOKE_OTHER_CONTRACT_PROCEDURE(RANDOM, BuyEntropy, ...)
src/contracts/Qusino.h:1133        INVOKE_OTHER_CONTRACT_PROCEDURE(RANDOM, BuyEntropy, ...)   # inside refillRandomBank()
```

Qusino is the most interesting variant because it **pre-banks** the entropy and derives a whole reserve from one seed — `src/contracts/Qusino.h:1102-1176`:

```c++
PUBLIC_PROCEDURE_WITH_LOCALS(refillRandomBank)
{
    if (qpi.invocationReward() > 0) { qpi.transfer(qpi.invocator(), qpi.invocationReward()); }  // takes NO payment
    if (state.get().rngReserveFilled > 0 || (... qpi.tick() < rngLastRefillTick + QUSINO_RNG_MIN_REFILL_TICK_GAP)) { return TOO_SOON; }
    ...
    INVOKE_OTHER_CONTRACT_PROCEDURE(RANDOM, BuyEntropy, locals.buyEntropyInput, locals.buyEntropyOutput, QUSINO_RNG_ENTROPY_FEE);
    locals.baseSeed = qpi.K12(locals.buyEntropyOutput.entropy);
    for (locals.seedIdx = 0; locals.seedIdx < QUSINO_RNG_RESERVE_SIZE; locals.seedIdx++)
    {
        locals.expanded = qpi.K12(m256i(baseSeed._0, baseSeed._1, baseSeed._2, baseSeed._3 ^ (seedIdx + 1ULL)));
        state.mut().rngReserve.set(locals.seedIdx, locals.expanded.u64._0);
    }
```

`refillRandomBank` is **permissionless and free**, and `QUSINO_RNG_COLLATERAL_TIER = 0` / `QUSINO_RNG_ENTROPY_BITS = 256` (`Qusino.h:94-95`) are the *same* `RANDOM` slot an attacker can read in the same tick. An attacker contract that calls `BuyEntropy` and then `refillRandomBank()` in one invocation seeds the entire game reserve with values it already knows (each value is a public `K12` chain), and can then place only the bets that win. The rate limit (`QUSINO_RNG_MIN_REFILL_TICK_GAP`) only gates *when*, which is precisely the variable the attacker needs to control.

---

## 3. Production configuration that makes it exploitable (all read from the repo — no RPC)

`src/contract_core/contract_def.h:409` — `contractDescriptions[]` (`{assetName, constructionEpoch, destructionEpoch, stateSize}`):

| idx | asset | constructionEpoch | destructionEpoch | stateSize | status |
|---|---|---|---|---|---|
| 3 | `RANDOM` | 88 | **10000** | 2 459 184 | live (entry point `BuyEntropy` = user proc 2) |
| 16 | `RL` | 182 | 10000 | 82 040 | live |
| 19 | `QRAFFLE` | 192 | 10000 | 48 368 712 | live |
| 23 | `QDUEL` | 199 | 10000 | 177 496 | live |
| 24 | `PULSE` | 204 | 10000 | 131 368 | live |
| 26 | `QUSINO` | 208 | 10000 | 432 432 088 | live |
| 30 | `QTREAT` | 233 | 10000 | 17 563 632 | live |

Every entry in the table has `destructionEpoch = 10000`, and the node at HEAD is parameterized for **epoch 239** (`a39d470 "update params for epoch 239 / v1.312.0"`), so all of the above are constructed and none is destroyed ⇒ the call graph is live in production code.

Constants (repo values, no external input needed):

```
Random.h:3            RANDOM_BITFEE                     = 100      quants / bit
RandomLottery.h:50-52 RL_RANDOM_ENTROPY_BITS            = 256
                      RL_RANDOM_COLLATERAL_TIER         = 0
                      RL_RANDOM_ENTROPY_FEE  = 100*256   = 25 600   quants
QDuel.h:5             QDUEL_MINIMUM_DUEL_AMOUNT         = 10 000   quants
QDuel.h:623           CreateRoom: stake >= 2*RL_RANDOM_ENTROPY_FEE = 51 200 quants  (min legal pot)
Qusino.h:94-95        QUSINO_RNG_ENTROPY_BITS = 256, QUSINO_RNG_COLLATERAL_TIER = 0
Qusino.h:1088-1090    refillRandomBank: permissionless, "Takes no payment from the caller"
```

No value outside the repo is required. The single production parameter that would *reduce* exposure is `RANDOM.bitFee` (a state value, = `RANDOM_BITFEE` after `INITIALIZE`) — it only changes the attacker's cost, not the reachability; and `QDUEL.minimumDuelAmount` (state value, `QDuel.h:520`), which the contract raises *above* the entropy fee, so no setting of it removes the attack.

---

## 4. PoC — executed locally against the clone (no RPC, no fork)

Built from the cloned sources in a Linux-adapted copy of `qubic/core` (`/home/user/build/qt`; see `AUDIT_LOG.md` for the build recipe). The PoC instantiates the **real** `RANDOM` contract state (`sizeof(RANDOM::StateData) = 2 459 184` == `contractDescriptions[3].stateSize`, asserted at run time), runs the **real** `BuyEntropy` user procedure through the production `QpiContextUserProcedureCall`, and replicates **`QDuel::GetWinnerPlayer`** byte-for-byte (same struct layout / same `K12` / same decision bit) to show the winner is pre-computable.

Source: `audit/audit_random_poc.cpp` (this repo). Commands:

```sh
cd /home/user/build/qt/b && ninja qubic_core_tests
cd /home/user/build/qt/b/test && ./qubic_core_tests --gtest_filter='AuditOracle.*'
```

Output (verbatim):

```
[ RUN      ] AuditOracle.EntropyReadIsReplayableAndPredictsQDuelSettlement
[poc] RANDOM stateSize=2459184 sizeof(StateData)=2459184
[poc] bitFee=100 sizeof(StateData)=2459184
[poc] tick=1000 entropy slot=0
[poc] call 1: attacker BuyEntropy
[poc] call 2: attacker BuyEntropy again
[poc] call 3: bystander BuyEntropy
[poc] call 4: victim (as the game) BuyEntropy
[poc] winner@tick1000 = ATTACKER
[poc] attacker-favorable rounds: 29/64
[poc] earnedAmount=102400 (4 reads x 25600)
[poc] favorableTicks=29/64 expectedReadsPerWin=56496 breakEvenStake=76800 quants (min legal stake = 51200)
[poc] stake=10000000 payoutOnWin=19974400 PnL=9917904 quants; win rate when joining = 1.0
[       OK ] AuditOracle.EntropyReadIsReplayableAndPredictsQDuelSettlement (262 ms)
[  PASSED  ] 1 test.
```

What each step proves (assertions are `ASSERT_TRUE`, so the test aborts on failure):

1. **call 1** — the attacker pays `256 × 100 = 25 600` quants and receives *exactly* the bits the oracle will serve to any consumer this tick (`bitsEqual(a1.entropy, live, 256)`).
2. **call 2** — a second, immediate read returns the **identical** bits and `entropy[slot]` is unchanged in state ⇒ the read is **non-consuming / replayable**; `earnedAmount = 4 × 25 600` (only accounting moved).
3. **call 3** — a **bystander** with no room, no stake and no commitment receives the same bits ⇒ the value is **not bound** to any participant or intent.
4. **call 4** — the bits the *game* would obtain (same tier 0, same 256 bits, `trustee = 0`) are bit-identical to the attacker's ⇒ the attacker's earlier read **is** the settlement value.
5. `duelWinner(...)` — replica of `QDuel::GetWinnerPlayer` over the attacker-obtained entropy: the outcome bit is `K12(entropy ‖ players ‖ tick).u64._0 & 1`, computable **before** any funds are committed. 29 of 64 candidate rounds favor the attacker, so with "join only when favorable" the realized win rate is 1.0 and the expected number of paid reads per win is 64/29 ≈ 2.2.
6. **PnL** — attacker puts in `stake = 10 000 000` quants, takes out `payoutOnWin = 19 974 400`, minus ~56 496 quants of oracle reads ⇒ **+9 917 904 quants (≈ 9.92 QU) per duel**, profit taken from the room creator's stake and the contract's escrowed shares. Break-even stake = 76 800 quants, i.e. **profitable for every room above 0.0768 QU**; no flash loan needed, no privileged role, and the loss side is capped at the read fee because the attacker simply does not join on unfavorable ticks.

Step-by-step math (the same numbers, in case you want to check without running it):

```
S  = room.amount chosen by the victim, S >= 2*25600 = 51200 (CreateRoom guard, QDuel.h:623)
c  = RL_RANDOM_ENTROPY_FEE = 25600
read cost per attempt = c ; expected attempts per win = 1/0.5 = 2 (attacker skips unfavorable ticks)
attacker outlay            = S + 2c                     (stake + two oracle reads)
attacker receive on win    = 2S - c - (dev/burn/share fees)   (QDuel.h:751-752 + CalculateRevenue)
net                        = S - 3c - fees  > 0  for  S > 76800 quants + fees
victim (room owner)        = -S with certainty once a favorable tick occurs (rooms live for TTL hours)
```

---

## 5. Impact / exploitability notes

* **Permissionless** — `BuyEntropy` and `ConnectToRoom`/`refillRandomBank` are registered user procedures; no owner, no whitelist, no pause. Any account (EOA or contract) can run them. The attacker's *only* capital requirement is the duel stake, which they recover with profit.
* **Triggerable by normal transactions** — no privileged actor, no relayer, no validator needed. (For completeness: `END_TICK` of `RANDOM` mixes provider reveals; the attacker does not need to influence it, only to *read after* it.)
* **Amplification** — the same primitive drains every game that settles on `RANDOM`: QDuel rooms (1:1 stakes), RandomLottery draws, Pulse tickets, QThirtyFour, and Qusino's coin-flip bankroll via `refillRandomBank` (which the attacker can *time and trigger themselves*). Value at risk = the game contracts' own QU balances plus every user's escrowed stake; TVL could not be read without RPC and is **not** used to qualify this finding (per the audit mandate).
* **Front-running variant (stronger, zero-latency)** — an attacker *contract* does: `BuyEntropy(256 bits, tier 0)` → `if (K12(...) & 1 == me) QDuel.ConnectToRoom(room) else return`. One invocation, one tick, guaranteed win. No timing race, no node access beyond broadcasting a transaction.
* Honest limitation of this PoC: it demonstrates the oracle primitive and the deterministic winner computation against the production `RANDOM` contract and the production `GetWinnerPlayer` code path. The end-to-end "open a room as victim, win as attacker" flow needs the full `QDuel` + `QX`/asset universe wiring (this Linux build cannot run `initSpecialEntities()` — see `AUDIT_LOG.md` §4), so that leg is documented as an exact code trace rather than an executed test.

---

## 6. Remediation (pick at least one of 1+2; 3 is defence-in-depth)

1. **Make the draw consuming and per-consumer.** In `BuyEntropy`, keep a per-(caller,tier) cursor or a global `drawCounter` and XOR it into the slot selection / into the returned bits, so two readers in the same tick cannot obtain the same value and a pre-read cannot be replayed against a later settlement.
2. **Bind settlement to a commitment made before the entropy is known.** Games should (a) take the bet/`ConnectToRoom` first, recording `K12(nonce ‖ entropyDigestOfFutureTick)` in state, and (b) settle with the entropy of a *later* tick than the one readable at commit time. Never read "the last finalized stream" in the same invocation that both decides participation and pays out.
3. **Reject contract invokers** on `QDuel.ConnectToRoom`, `Qusino.coinFlip`, `RandomLottery`, `Pulse`, `QThirtyFour` (`qpi.isContractId(qpi.invocator())` — the pattern already used in `Random.h:226`) to at least kill the zero-latency variant, and charge a refundable *anti-lookahead* deposit for reads.
