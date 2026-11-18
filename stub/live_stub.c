/* Boardroom: code that runs inside the running game (FL_2027.exe).
 *
 * The app copies this blob into the game and points the start of the game's
 * day-end routine at `hook.S`, which calls `run_requests` on the game's main
 * thread once per in-game day, before the game runs its own deal lists. Each
 * armed request becomes an agreed deal built with the game's own routines,
 * as an accepted offer is: a TeamContract (club to club) and/or a
 * PlayerContract (the new contract), registered in the ContractStation with
 * state 4 and handed to the TransferManager's deal list. The game then
 * moves the player itself (at this day end, or on the arrival date).
 *
 * Game routine addresses are filled in by the app (`Ctx`) after it checked
 * their first bytes. Build: stub/build.sh (mingw), output src/live_stub.bin.
 * Freestanding: no C runtime, no imports, no globals; everything is reached
 * through the `Ctx` pointer the hook passes.
 */
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef void *Handle; /* pointer to the game's 0x18-byte control block */

enum { SLOT_FREE = 0, SLOT_ARMED = 1, SLOT_DONE = 2, SLOT_FAILED = 3 };
enum { KIND_TRANSFER = 1, KIND_FREE_AGENT = 2, KIND_LOAN = 3 };
enum {
    ERR_NONE = 0,
    ERR_NO_STATION = 1,
    ERR_NO_PLAYER = 2,
    ERR_BUILD_DEAL = 3,
    ERR_BUILD_CONTRACT = 4,
    ERR_KIND = 5,
};
#define STATE_AGREED 4
#define NSLOTS 8

/* One deal, written by the app. */
typedef struct {
    volatile u32 state;
    u32 kind;
    u64 key;      /* player key: internal id | player id << 32 */
    u32 buyer;    /* club the player joins (team index) */
    u32 seller;   /* club he leaves (team index) */
    u32 fee;      /* fee / 100 (transfer or loan fee) */
    u32 salary;   /* new contract: salary / 100 */
    u32 end;      /* new contract end, packed date */
    u32 release;  /* release clause / 100, 0 = none */
    u32 loan_end; /* loan end, packed date */
    u32 date;     /* execution date, packed; 0 = today */
    u32 error;
    u32 user;     /* 1 = the user's club buys or sells: the game charges the fee and wages */
    u32 reserved[2];
} Slot;

typedef struct {
    u64 magic; /* "BOARDRM1" */
    u32 version;
    u32 runs; /* day ends seen */
    /* absolute addresses, filled in by the app */
    u64 contract_station; /* &(ContractStation*) */
    u64 null_date;        /* &u32 */
    void (*today)(u32 *out);
    void *(*world)(void);
    void *(*find_player)(void *table, u64 key);
    void *(*build_offer)(void *unused, u64 key, u32 buyer, u32 seller, u32 loan_end, u32 fee);
    void *(*make_player_contract)(void *unused, u64 key, u32 club, u32 end, u32 salary);
    Handle *(*wrap_handle)(void *obj, Handle *out);
    Handle *(*copy_handle)(Handle *dst, Handle *src);
    void (*free_handle)(Handle *h);
    void (*begin_edit)(void *obj);
    void (*commit_edit)(void *obj);
    void (*station_add)(void *station, Handle *h, u32 state);
    void (*deal_list)(void *manager, void *group, u32 kind);
    Handle *(*negotiations)(Handle *out);
    void *(*get_object)(Handle *h);
    void (*charge)(void *negotiations, Handle *h); /* consumes h */
    Slot slots[NSLOTS];
} Ctx;

_Static_assert(sizeof(Slot) == 0x40, "slot layout is shared with the app");
_Static_assert(__builtin_offsetof(Ctx, today) == 0x20, "ctx layout is shared with the app");
_Static_assert(__builtin_offsetof(Ctx, slots) == 0x98, "ctx layout is shared with the app");

/* Edit copies of an object's meta (club at +8, date at +0xC) and terms. */
static u8 *edit_meta(void *obj) { return *(u8 **)((u8 *)obj + 0xB0); }
static u8 *edit_terms(void *obj) { return *(u8 **)((u8 *)obj + 0xE0); }

static void set_date(Ctx *c, void *obj, u32 date)
{
    c->begin_edit(obj);
    *(u32 *)(edit_meta(obj) + 0xC) = date;
    c->commit_edit(obj);
}

/* An object nothing owns yet (not registered): the entity's Destroy (vtbl+0x20). */
static void destroy(void *obj)
{
    ((void (**)(void *)) * (void **)obj)[4](obj);
}

/* Agree one deal; returns an error code. */
static u32 agree(Ctx *c, void *manager, Slot *s)
{
    void *station = *(void **)c->contract_station;
    if (!station) return ERR_NO_STATION;
    void *table = *(void **)((u8 *)c->world() + 0x48);
    if (!c->find_player(table, s->key)) return ERR_NO_PLAYER;
    u32 today;
    c->today(&today);
    u32 date = s->date ? s->date : today;
    u32 null_date = *(u32 *)c->null_date;

    void *deal = 0, *contract = 0;
    if (s->kind == KIND_TRANSFER || s->kind == KIND_LOAN) {
        u32 loan_end = s->kind == KIND_LOAN ? s->loan_end : null_date;
        /* build_offer values a zero fee itself; the agreed fee is set below. */
        deal = c->build_offer(0, s->key, s->buyer, s->seller, loan_end, s->fee ? s->fee : 1);
        if (!deal) return ERR_BUILD_DEAL;
        c->begin_edit(deal);
        *(u32 *)(edit_terms(deal) + 0xC) = s->fee;
        *(u32 *)(edit_terms(deal) + 0x10) = s->fee;
        c->commit_edit(deal);
    } else if (s->kind != KIND_FREE_AGENT) {
        return ERR_KIND;
    }
    if (s->kind == KIND_TRANSFER || s->kind == KIND_FREE_AGENT) {
        contract = c->make_player_contract(0, s->key, s->buyer, s->end, s->salary);
        if (!contract) {
            if (deal) destroy(deal);
            return ERR_BUILD_CONTRACT;
        }
        c->begin_edit(contract);
        *(u32 *)(edit_terms(contract) + 0x10) = s->release;
        c->commit_edit(contract);
    }

    Handle hd = 0, hc = 0;
    if (deal) c->wrap_handle(deal, &hd);
    if (contract) c->wrap_handle(contract, &hc);
    c->begin_edit(station);
    if (deal) c->station_add(station, &hd, STATE_AGREED);
    if (contract) c->station_add(station, &hc, STATE_AGREED);
    c->commit_edit(station);
    if (deal) set_date(c, deal, date);
    if (contract) set_date(c, contract, date);
    /* As the user's accept path does for every agreed item: the game
     * charges (buyer) or credits (seller) the fee and books the wages. */
    if (s->user) {
        Handle hn = 0;
        c->negotiations(&hn);
        void *neg = hn ? c->get_object(&hn) : 0;
        if (neg) {
            Handle t;
            if (deal) c->charge(neg, c->copy_handle(&t, &hd));
            if (contract) c->charge(neg, c->copy_handle(&t, &hc));
        }
        if (hn) c->free_handle(&hn);
    }

    /* The group as accept_offer hands it on: deal first, then the contract. */
    Handle members[2];
    u32 n = 0;
    if (deal) c->copy_handle(&members[n++], &hd);
    if (contract) c->copy_handle(&members[n++], &hc);
    Handle *group[3] = { members, members + n, members + n };
    c->deal_list(manager, group, 0);
    while (n) c->free_handle(&members[--n]);
    if (deal) c->free_handle(&hd);
    if (contract) c->free_handle(&hc);
    return ERR_NONE;
}

/* Called by hook.S with the game's TransferManager (day_end's `this`). */
void run_requests(Ctx *c, void *manager)
{
    c->runs++;
    for (u32 i = 0; i < NSLOTS; i++) {
        Slot *s = &c->slots[i];
        if (s->state != SLOT_ARMED) continue;
        u32 err = agree(c, manager, s);
        s->error = err;
        s->state = err ? SLOT_FAILED : SLOT_DONE;
    }
}
