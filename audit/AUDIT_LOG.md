# Audit log — Qubic protocol (node + system contracts + ecosystem)

Everything below was produced without any RPC/fork access: all facts come from the
clones registered here, from their build/test harnesses, or from arithmetic.

## 1. Owner / space enumeration

* GitHub orgs belonging to the protocol, enumerated via the GitHub REST API
  (`GET /orgs/<login>`) on 2026-11-12:
  * `qubic` — 77 public repos (created 2023-10-08) → canonical node repo `qubic/core` lives here.
  * `qubic-network` — 5 public repos (created 2023-12-01).
  * `qubic-foundation`, `quubic` — **do not exist** (API returns no object).
* No GitLab / Bitbucket / docs-only space is referenced by the repos (no
  `.gitmodules`, no CI config pointing at another host for in-scope code).
* Diff check: every repo name in both orgs is present in the local clone set except
  `qubic/contract-verify` (see §7). Command used:
  `curl -s https://api.github.com/orgs/qubic/repos?per_page=100` vs `foundation/manifest.tsv`
  → `in org but NOT in manifest: ['contract-verify']`, `in manifest but NOT in org: []`.

## 2. Cloned repos and registered commit hashes (76)

`git clone` of every repo of both orgs into `/home/user/foundation/repos/`, default branch,
HEAD registered in `/home/user/foundation/manifest.tsv`
(format `name<TAB>url<TAB>branch<TAB>HEAD<TAB>archived<TAB>fork`).
Additionally the audit target itself:

* `/home/user/qubic` — `8cd0dbfa559b2b7f5666853649ebe352b1a47578`
  (`Merge pull request #1064 from qubic/develop`, 2026-11-04) — **the audited snapshot, left unpatched**.
* `/home/user/foundation/repos/core` — full clone (all branches + tags),
  HEAD `9af0d56f2566da9b8156dae068a2e72109bd30ae` (main, 2026-11-11),
  tags through `v1.312.0`.

Delta review of the audited snapshot against current production HEAD:

```
$ git -C /home/user/foundation/repos/core log --format="%h %ad %s" --date=short 8cd0dbfa..origin/main
9af0d56 2026-11-11 Merge pull request #1069 from qubic/develop
64559af 2026-11-10 Increase tick vote signing difficulty to 0x0000134B
a39d470 2026-11-10 update params for epoch 239 / v1.312.0
8004bf9 2026-11-10 Update bpp9000 params and speedup. (#1067)
3d5a79c 2026-11-08 Merge pull request #1065 from qubic/feature/2026-11-07-SigningDifficultyC0F9
... (11 commits total; none touch src/contracts/Random.h, QDuel.h, Qusino.h, RandomLottery.h)
$ git log --format="%h %ad %s" --date=short 8cd0dbfa..origin/main -- src/contracts/Random.h src/contracts/QDuel.h src/contracts/Qusino.h src/contracts/RandomLottery.h
(empty)
```

Active bugfix branches read (not in the snapshot, informational):
`2026-10-13-reject-transfer-to-inactive-sc`, `2026-10-16-skip-post-incomming-transfer-inactive-sc`,
`2026-10-28-ContractStateFileSize`, `2026-11-03-FileIoLoadFclose`, `2026-10-28-RemoveQxTwinFixSweep`.

Git LFS: `git lfs` is not installed in the sandbox, therefore `git lfs pull` could not be run.
Checked for impact instead: `core` has **no `.gitattributes`** and
`grep -rl "^version https://git-lfs" src/ test/` → no hits ⇒ no LFS-tracked content in the audited tree.

## 3. Production parameters / addresses / module mapping (no RPC)

Qubic has **no** `deployments/`, `broadcast/`, `scripts/`, `genesis.json`, chainspec,
`Anchor.toml`, `Move.toml`, `foundry.toml`, `hardhat.config.js` or `package.json`
(verified present/absent in `core`; all absent). System contracts are **compiled into the node
binary**, so the "deployment registry" is source:

* `src/contract_core/contract_def.h:409` — `contractDescriptions[]`
  = `{char assetName[8]; uint16 constructionEpoch; uint16 destructionEpoch; uint64 stateSize}`
  for all 36 contract slots → this is the address/program-id table **and** the liveness table.
* `src/contract_core/contract_def.h:1-408` — `#define <NAME>_CONTRACT_INDEX` per contract,
  plus `#include "contracts/<X>.h"` (each contract's `REGISTER_USER_FUNCTIONS_AND_PROCEDURES()`
  gives the entry-point ids that were used to build the call graph).
* `src/public_settings.h` (`MAX_AMOUNT`, `SPECTRUM_CAPACITY`, `NUMBER_OF_COMPUTORS`,
  `MAX_SIZE_OF_CONTRACT_LOCALS = 32768`, `MAX_NESTED_CONTRACT_CALLS = 10`),
  `src/private_settings.h`, `src/frozen_addresses.h` (frozen/blocked identities → escrow).
* Per-contract parameter blocks: the `constexpr` header prefix of every `src/contracts/*.h`
  (fees, caps, epochs) — e.g. `Random.h:3 RANDOM_BITFEE = 100`,
  `RandomLottery.h:50-52`, `QDuel.h:5,520`, `Qusino.h:94-95`, `QVAULT.h:11-13`,
  `Qpump.h:24-110`, `Qearn.h:11-40`.
* Epoch-parameter release commits as "deploy configs": `a39d470 "update params for epoch 239 / v1.312.0"`,
  `b3aa99e "Apply epoch 238 release patches (QIP, QVAULT, QSWAP)"`,
  `e492598 "QPUMP: set construction epoch to 238"`.
* Liveness conclusion used for criterion 3: construction epochs span **66 → 238** and
  **all 36 `destructionEpoch` values are 10000**, while the node is parameterized for epoch 239+
  ⇒ every contract in the table (incl. `QPUMP` 238, `QTREAT` 233, `QPAYHUB` 231, `GGWP` 218,
  `ESCROW` 210, `QUSINO` 208, `VOTTUN` 206, `PULSE` 204, `QDUEL` 199, `RL` 182, `RANDOM` 88)
  is constructed and not destroyed in production.

Registered table (from the executed harness run, §5):

```
0 sys 0 8192 | 1 QX 66 | 2 QTRY 72 | 3 RANDOM 88 | 4 QUTIL 99 | 5 MLM 112 (IPO)
6 GQMPROP 123 709184 | 7 SWATCH 123 27040 | 8 CCF 127 493584 | 9 QEARN 137 214171656
10 QVAULT 138 581841120 | 11 MSVAULT 149 376448096 | 12 QBAY 154 506986632
13 QSWAP 171 286326928 | 14 NOST 172 4893616 | 15 QDRAW 179 32864 | 16 RL 182 82040
17 QBOND 182 357237432 | 18 QIP 189 26906696 | 19 QRAFFLE 192 48368712 | 20 QRWA 197 30677232
21 QRP 199 27040 | 22 QTF 199 82608 | 23 QDUEL 199 177496 | 24 PULSE 204 131368
25 VOTTUN 206 176264 | 26 QUSINO 208 432432088 | 27 ESCROW 210 403505400 | 28 GGWP 218 3669088
29 QPAYHUB 231 44116552 | 30 QTREAT 233 17563632 | 31 QPUMP 238 238502144
32-35 TESTEXA/B/C/D 138-155 (test-only, not live: excluded from findings)
```

## 4. Local repro / PoC environment (built from the clone, reproducible)

Toolchain: g++ 12.2.0, cmake 4.4.4, ninja, 2 cores / 3 GB RAM, `-march=native` (AVX-512 present).
A patched, git-free copy of `core` was made in `/home/user/build/qt` so the audit clone stays pristine.
Build (works, ~7 s incremental):

```sh
cd /home/user/build/qt/b && cmake .. -G Ninja -D CMAKE_C_COMPILER=gcc -D CMAKE_CXX_COMPILER=g++ \
  -D BUILD_TESTS=ON -D BUILD_EFI=OFF -D BUILD_BENCHMARK=OFF -D USE_SANITIZER=OFF \
  -D CMAKE_BUILD_TYPE=Release -D ENABLE_AVX512=ON && ninja qubic_core_tests
```

Linux portability patches, all confined to `/home/user/build/qt` (never in `/home/user/qubic`):
`compat/msvc_compat.h` (`__sync_*` for `_Interlocked*`, `_rotl*`, `__lzcnt*`, `__popcnt*`,
`_umul128/_udiv128/_mul128`, `__shiftright128`, `_BitScan*`, `_byteswap_*`, `__cpuid` asm,
`wcstombs_s`, `_fseeki64/_ftelli64/_wfopen_s`, `operator"" i64/ull`);
`cmake/CompilerSetup.cmake` (`-w -fshort-wchar -fpermissive -fms-extensions -D__cdecl= -D_MSVC_COMPAT
-include compat/msvc_compat.h`, `-march=native -mbmi -mbmi2 -mlzcnt -mpopcnt -mf16c`, `-no-pie`);
`CHAR16 → wchar_t` in `common_types.h`/`ProcessorBind.h`/`uefi.h`; `platform/memory_util.h`
(`allocPoolWithErrorLog` → `calloc(1,size)` instead of `malloc`+`memset` — the multi-GB zero-fill
of `spectrum`/`digests`/`assets` exhausted the 3 GB sandbox and killed the process before any test ran);
`test/contract_testing.h` (`initEmptySpectrum`/`initEmptyUniverse` memsets dropped, `INIT_CONTRACT` → `calloc`);
`test/audit_shims.cpp` (`InternalSetJump`/`InternalLongJump` via `setjmp`/`longjmp` + `printf` forwarder);
`stdlib_impl.cpp` (`now_ms() → 0`); `test/CMakeLists.txt` (test target = `audit_poc.cpp`,
`audit_random_poc.cpp`, `common_def.cpp`, `audit_shims.cpp`, `stdlib_impl.cpp`);
`src/logging/logging.h` + `test/logging_test.h` (`ENABLED_LOGGING 0`, all `LOG_*` 0 — the logger's
`VirtualMemory` page-table backing is EFI/Windows-only and faults on Linux).
Note: `-march=native` means the harness guarantees **deterministic contract math**, not mainnet digest parity.

Known limitation of this build (documented, not worked around): `initSpecialEntities()` faults in the
FourQ/`setRandomValue()` path, which is what the *upstream* `ContractTesting` fixture calls first, so the
52 pre-existing suites (`test/contract_qduel.cpp`, `test/contract_qx.cpp`, …) cannot be executed here yet.
The oracle PoC therefore uses a reduced bootstrap that performs the same init steps that do work
(`commonBuffers.init(1)`, `initContractExec()`, `initSpectrum()`, `initAssets()`) and calls the real
procedure through the production `QpiContextUserProcedureCall`.

## 5. Local tests / PoCs actually run

```
$ cd /home/user/build/qt/b/test && ./qubic_core_tests --gtest_filter='Audit.*'
[stage] initContractExec
initContractExec -> 1
[stage] initSpectrum
initSpectrum -> 1
[stage] initAssets
initAssets -> 1
[       OK ] Audit.StagedInit (1222 ms)        # global node state + 36-contract table verified
```

```
$ cd /home/user/build/qt/b/test && ./qubic_core_tests --gtest_filter='AuditOracle.*'
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

PoC source committed next to this log: `audit/audit_random_poc.cpp`
(copy into `<core>/test/` and add to `test/CMakeLists.txt` to re-run).

Static analysis passes used to build the candidate set (all reproducible with grep/python over the clone):

* every `qpi.transfer(qpi.invocator(), qpi.invocationReward())` not immediately followed by `return`
  → ~180 sites in MsVault, Nostromo, Pulse, QPayhub, QIP, Qusino, QRaffle, Qbay, QDuel, QTREAT, Qx, qRWA;
  then a per-procedure scan for *two* refunds with no `return` in between → 21 candidates, each
  hand-inspected (Escrow.h:780-816, QIP.h:792-804, QVAULT.h:2118-2165, Qbay.h:2377-2393,
  Random.h:198/219, QTREAT.h:548-568/638-649/710-727/998-1028) → all are branch-guarded
  refund-or-charge paths; **no double refund** found.
* every `qpi.transferShareOwnershipAndPossession/acquireShares/releaseShares/issueAsset` whose return
  value is discarded → ~70 sites, triaged (see ledger).
* every `(uintNN)amount` / `(sintNN)shares` cast on a money-carrying expression → 15 hits, all in
  Qbay/QVAULT/QRaffle/MsVault, checked against the field's real width (no truncation: `pricePerOneQcap`
  is `uint64` × `uint32` amount).

## 6. Findings ledger

| ID | Severity | Component | Location | Status |
|---|---|---|---|---|
| F-01 | **CRITICAL** | `RANDOM` oracle + every game consumer | `src/contracts/Random.h:165-221`, `src/contracts/QDuel.h:686-790,1198-1215`, `Qusino.h:1102-1176`, `RandomLottery.h:648`, `Pulse.h:1796`, `QThirtyFour.h:1609`, `QRaffle.h:2307`, `QTREAT.h:1807` | **all four criteria met, executed PoC** → `AUDIT_CRITICAL_RANDOM_ORACLE.md` |
| F-02 | Medium | `QVAULT.buyQcap` | `src/contracts/QVAULT.h:1994-2105`: `:2089` ignores the `QX.TransferShareManagementRights` output, `:2091` ignores the `transferShareOwnershipAndPossession` return value, `:2093-2096` updates `qcapSoldAmount`/`restSaleAmount` unconditionally — while `QVAULT.h:865` (the `stake` path) performs the *same* transfer **and** checks `< 0`, so the missing check in `buyQcap` is a deviation from the contract's own convention | Verified by code; if the share hand-out fails the payer keeps paying and `qcapSoldAmount`/`restSaleAmount` still move ⇒ user fund loss + accounting divergence. **No attacker profit → not critical.** |
| F-03 | Low / informational | `Qbay.acceptOffer` | `src/contracts/Qbay.h:1873-1899` | Verified by code: the CFB branch (`paymentMethodOfAsk == 1`) deducts `creatorFee` + `marketFee` but never `shareHolderFee` (`QBAY_FEE_NFT_SALE_SHAREHOLDERS`, 10/1000) that the QU branch collects → ~1 % fee leakage to shareholders. No attacker profit, no user loss (seller keeps it) → not critical. |
| F-04 | Medium (availability) | `QX.AddToAskOrder`, `QEARN._RemoveGapsInLockerArray` | `src/contracts/Qx.h:540-760` (unconditional `while (true)` scan of `_assetOrders`, comment "Impossible for the corresponding asset order to not exist"); `src/contracts/Qearn.h` (`ASSERT(st <= en)` on `_epochIndex`) | Trigger not proven; a missed `ASSERT` in a system procedure sets `contractError` (in this harness `src/platform/assert.h:6` maps `ASSERT`→`EXPECT_TRUE`, so a failed EXPECT in a PoC = a production abort). Excluded from critical per the mandate (no fund impact proven). |
| F-05 | — | "refund-then-continue" families | see §5 | **cleared**: no double-refund / partial-refund loss found in the 21 candidates. |
| F-06 | — | `Qpump` curve/fee math | `src/contracts/Qpump.h:24-155` | **cleared**: `QPUMP_COST_LINEAR = QPUMP_COST_DENOMINATOR = 1420000000000`, `SLOPE = 9000`, full-curve raise 3 905 000 000 — checked by hand and by the `STATIC_ASSERT`s; `1048576`-scaled `divQuanti2Share`/`mulShare2Quanti` round-trips consistently. |
| F-07 | — | `QSwap.AddLiquidity`/`SwapExactAssetForQu` refund ordering | `src/contracts/Qswap.h:1230-1420, 2035-2200` | **cleared**: every early exit refunds `invocationReward()` then returns; the swap path re-reads pool share balances before/after the transfer and rolls back any partial receipt. |

### Invariants that were verified (and hold)

* QPI arithmetic is abort-free: `QPI::div(a,b) = b ? a/b : 0`, `mod(a,b) = b ? a%b : 0`
  (`src/qpi/qpi.h:54-64`), `smul`/`sadd` clamp — so "div-by-zero panic" style findings are impossible here.
* `__transfer` (`src/qpi/impl/qpi_spectrum_impl.h:142-200`) rejects `amount < 0 || amount > MAX_AMOUNT`,
  rejects transfers whose destination contract is in `contractError`, before `constructionEpoch` or at/after
  `destructionEpoch` (the epoch-238/1.312 fix `812ab5b "Reject transfer to inactive sc."` + `ddf2789`/`12aa66b`
  "skip POST_INCOMING_TRANSFER for a failed SC" are **in** the audited snapshot), and requires
  `energy(index) >= amount`. Contracts that discard the negative return code are the F-02 class of bug.
* Balances are cumulative `incomingAmount - outgoingAmount` (never reset per epoch); `QPI::getEntity`
  zeroes the record when `spectrumIndex < 0`; `increaseEnergy`/`decreaseEnergy` maintain `spectrumDirtyList`
  with a 16384 cap that flips `spectrumDirtyOverflow` and forces a full digest scan (availability, not funds).
* QEARN epoch accounting re-derived from source and consistent: `lockedEpoch = epoch - 52`,
  `rewardPercent = _epochBonusAmount*10000000/_totalLockedAmount`, `_rewardAmount = locked*rewardPercent/10000000`,
  `MAX_LOCK_AMOUNT = 1e12` caps the whole pool, so the `BEGIN_EPOCH` per-locked-unit loop
  (`for i < _totalLockedAmount` when `< 180`) and `_RemoveGapsInLockerArray`'s 1000-slot-per-account scan cannot be
  scaled into a fund-losing DoS (worst case bounded by `MAX_LOCK_AMOUNT / MINIMUM_LOCKING_AMOUNT = 100 000` buckets).
* QX/QSwap/QBond/QVAULT share-lock accounting: the twin-issuer/truncated-issuer collisions were fixed before
  this snapshot (`ddd4735`, `4b6988d`, `5666651`, `33bc7fd`, `bebf3b2`, `06c2abb`, `d695662` — all ancestors of
  `8cd0dbfa`), and the K12 LP-key/legacy-key split (`Qswap.h:132-720`) is wired consistently in-repo.

**Bottom line for the rest of scope:** apart from F-01, no bug was found that satisfies all four critical
criteria — i.e. for the node/consensus/p2p/mempool/VM layer, the precompile-equivalents (`qpi/*_impl.h`), and
the other system contracts: **no crítico en código** en esta fase (with F-02/F-03/F-04 as non-critical
correctness/availability issues listed above).

## 7. Blocked / inaccessible (coverage gaps — coverage is NOT complete)

* `qubic/contract-verify` — cloned at top level, **submodule unreachable**
  (`satya-das/cppparser.git` → not found / private): `[FAIL] contract-verify` in
  `/home/user/foundation/clone_report.txt`. It is a contract-verification tool, not a protocol runtime
  component, but it is listed as blocked rather than covered.
* Private repos of either org (if any) are not enumerable without credentials → not covered.
* Git LFS content: not fetched (`git-lfs` absent in sandbox); verified no LFS pointers exist under
  `src/` or `test/`, so no audited file is affected.
* Live chain state (TVL, real balances, real `QDUEL` room sizes, `RANDOM.bitFee` overrides at runtime):
  intentionally **not** read — the mandate forbids RPC. Per the golden rule this was *not* used to dismiss
  any protocol; where an external value matters it is named explicitly (F-01 §3: `RANDOM.bitFee`,
  `QDuel.minimumDuelAmount`).
* Upstream `test/*` suites: not executable in this Linux build (see §4 limitation) ⇒ only the two
  tests listed in §5 were run here.

## 8. Repo inventory (registered HEADs)

| repo | default branch | HEAD (registered) | flags |
|---|---|---|---|
| `.github` | `main` | `303afcaee97a` | — |
| `aio-qubic-dev-kit` | `master` | `d3d900d94567` | — |
| `archive-query-service` | `main` | `82c0fdeaa5a6` | — |
| `archiver-db-migrator` | `main` | `aa5e652600ee` | — |
| `core` | `main` | `56c2536d2c32` | — |
| `core-bob` | `master` | `e09ad23c6809` | — |
| `core-docker` | `main` | `b7a5f081d64c` | — |
| `core-lite` | `main` | `e6a13ab688ee` | — |
| `dapps-explorer` | `main` | `4c043114089d` | ARCHIVED |
| `docs` | `main` | `ef21138c1526` | — |
| `doge-connect` | `main` | `c810c04c6545` | — |
| `explorer-frontend` | `main` | `b7f269780b44` | — |
| `go-archiver` | `main` | `506f0ff4b79c` | — |
| `go-archiver-v2` | `main` | `bade88c5f6e8` | — |
| `go-data-publisher` | `main` | `87439f9e3c84` | — |
| `go-events` | `main` | `131d2fe6fc7c` | — |
| `go-events-consumer` | `main` | `0c9dca788c14` | ARCHIVED |
| `go-events-publisher` | `main` | `f155eb68a9c3` | ARCHIVED |
| `go-log-data-publisher` | `main` | `6c5f2bf64845` | — |
| `go-node-connector` | `main` | `572c5fd53e71` | — |
| `go-node-fetcher` | `main` | `3acd00482f43` | ARCHIVED |
| `go-qubic` | `main` | `cb81a459e786` | — |
| `go-qubic-nodes` | `main` | `4b3b81e61fa5` | — |
| `go-schnorrq` | `main` | `4f126bd34cfd` | — |
| `go-transactions-consumer` | `main` | `8db295b313b6` | ARCHIVED |
| `go-transfers` | `main` | `c3f47f3d6c25` | ARCHIVED |
| `grants` | `main` | `12d9f9b522a5` | — |
| `integration` | `main` | `f6f5d5720230` | — |
| `java-archiver-proto` | `main` | `61b52574b95f` | — |
| `kafka-streams-services` | `main` | `b0e1687b3853` | — |
| `license` | `main` | `065d0c1dc0aa` | — |
| `live-websocket` | `main` | `0232bc010255` | — |
| `network-guardians` | `main` | `2e3bd425ceca` | — |
| `oc-machine` | `main` | `2dcf678f0bd3` | — |
| `oc-mock-service` | `main` | `7c106a0726d9` | — |
| `oracle-machine` | `main` | `72192dae250c` | — |
| `org.qubic.proposal` | `main` | `42c763092004` | — |
| `outsourced-computing` | `main` | `e4b23ea9b283` | — |
| `proposal` | `main` | `4b2ffb0f7621` | — |
| `proposals-frontend` | `main` | `d5f247082283` | — |
| `qct` | `main` | `1c005da67ceb` | — |
| `Qiner` | `main` | `71cfbd66e4b6` | — |
| `qlogging` | `master` | `aaa7fad89372` | — |
| `qubic-aggregation` | `main` | `e27668f8a207` | — |
| `qubic-cli` | `main` | `76d55e0c35e2` | — |
| `qubic-csharp` | `main` | `bb897dbf9e39` | — |
| `qubic-dev-kit` | `main` | `3b1221d66ffb` | ARCHIVED |
| `qubic-hackathon` | `main` | `ac830bd518b5` | — |
| `qubic-http` | `main` | `436e7a2ca64a` | — |
| `qubic-mm-snap` | `main` | `26a8772f8bc0` | — |
| `qubic-stats-service` | `main` | `4d91bb86e519` | — |
| `qubic-transfer-watcher` | `main` | `e651e298700f` | — |
| `qubic-typescript` | `main` | `a4cfb4075097` | — |
| `Qubic.NET` | `main` | `33e8e236c31d` | — |
| `Qubic.NET.Toolkit` | `master` | `87d7f8db7e7b` | — |
| `Qubic.NET.Wallet` | `master` | `1cef7b3ebaa6` | — |
| `Qubic.NET.Wallet.Mobile` | `master` | `6ff78e1b43e9` | — |
| `quottery-frontend` | `main` | `bb35ea117748` | ARCHIVED |
| `qx-frontend` | `main` | `7e03722ec2c3` | ARCHIVED |
| `qx-service` | `main` | `172d63a2ca96` | — |
| `react-ui` | `main` | `1b59dd183e71` | — |
| `roadmap` | `main` | `8db4d740fd14` | — |
| `static` | `main` | `effa4f3871fa` | — |
| `ts-library` | `main` | `50355856f0f7` | — |
| `ts-library-wrapper` | `main` | `41a77b122df9` | — |
| `ts-types` | `main` | `50ea6566fa02` | — |
| `ts-vault-library` | `main` | `8c305bcab4ac` | — |
| `voting-frontend` | `main` | `d70d9028debc` | — |
| `wallet` | `main` | `d551ab0d9619` | — |
| `wallet-app` | `main` | `85bfd501365b` | — |
| `wallet-app-dapp` | `main` | `5142b5196e01` | — |
| `wallet-extension` | `dev` | `1e3d76040c06` | — |
| `wallet-extension-dapp` | `main` | `a1da089e0eba` | — |
| `whitepaper` | `master` | `15dc18f85df4` | — |
| `wiki` | `security/workflow-hardening` | `f167eac66e10` | — |
| `ZKProofDemo` | `main` | `79c2b635cc51` | — |
