#define CHECK_TEST_NAME "deathboobs-transaction"
#include <testing/check.h>

#include <deathboobs/transaction.h>
#include <sys/file.h>

#ifdef DEATHBOOBS_NATIVE_TEST
#include <punos/syscall/syscall.h>

typedef enum CrashPoint {
    NO_CRASH,
    AFTER_FIRST_WRITE,
    BEFORE_UNLINK
} CrashPoint;

static CrashPoint crash_point;
ErrorCode __real_file_remove(const String *path);
ErrorCode __real_deathboobs_storage_page_write(DeathboobsStorage *storage,
                                              const DeathboobsPage *page);

ErrorCode __wrap_file_remove(const String *path)
{
    if (crash_point == BEFORE_UNLINK) {
        exit(42);
    }
    return __real_file_remove(path);
}

ErrorCode __wrap_deathboobs_storage_page_write(DeathboobsStorage *storage,
                                              const DeathboobsPage *page)
{
    ErrorCode error = __real_deathboobs_storage_page_write(storage, page);
    if (error == CORE_OK && crash_point == AFTER_FIRST_WRITE) {
        exit(42);
    }
    return error;
}
#endif

#define TEST_DB "/tmp/deathboobs_transaction_runtime.db"
#define TEST_JOURNAL TEST_DB ".journal"

static String path = {TEST_DB, sizeof(TEST_DB) - 1};
static String journal = {TEST_JOURNAL, sizeof(TEST_JOURNAL) - 1};

static DeathboobsPage make_page(DeathboobsPageId id, u8 value)
{
    return (DeathboobsPage){
        .id = id,
        .type = DEATHBOOBS_PAGE_TYPE_METADATA,
        .generation = value,
        .flags = 2,
        .payload_size = 1,
        .payload = {value}
    };
}

static bool page_is(DeathboobsStorage *storage, u64 id, u8 expected)
{
    DeathboobsPage page;
    return deathboobs_storage_page_read(storage, id, &page) == CORE_OK &&
           page.id == id && page.payload_size == 1 && page.flags == 2 &&
           page.generation == expected && page.payload[0] == expected;
}

#ifdef DEATHBOOBS_NATIVE_TEST
static void test_process_lock(void)
{
    pid_t pid = fork();
    EXPECT(40, pid >= 0);
    if (pid == 0) {
        DeathboobsStorage *storage = NULL;
        exit(deathboobs_storage_open(&path, &storage) == CORE_WOULD_BLOCK
                 ? 0 : 1);
    }
    i32 status;
    EXPECT(41, waitpid(pid, &status, 0) == pid && status == 0);
}

static void test_crashed_process(CrashPoint point)
{
    pid_t pid = fork();
    EXPECT(42, pid >= 0);
    if (pid == 0) {
        DeathboobsStorage *storage = NULL;
        DeathboobsTransaction *tx = NULL;
        DeathboobsPage one = make_page(1, 91);
        DeathboobsPage two = make_page(2, 92);
        EXPECT(43, deathboobs_transaction_open(&path, NULL, &storage) == CORE_OK);
        EXPECT(44, deathboobs_transaction_begin(storage, NULL, &tx) == CORE_OK);
        EXPECT(45, deathboobs_transaction_page_write(tx, &one) == CORE_OK);
        EXPECT(46, deathboobs_transaction_page_write(tx, &two) == CORE_OK);
        crash_point = point;
        (void)deathboobs_transaction_commit(&tx);
        exit(47);
    }
    i32 status;
    EXPECT(48, waitpid(pid, &status, 0) == pid && status == (42 << 8));

    DeathboobsStorage *storage = NULL;
    EXPECT(53, deathboobs_storage_open(&path, &storage) == CORE_OK);
    EXPECT(54, page_is(storage, 1, 91) &&
               page_is(storage, 2, point == AFTER_FIRST_WRITE ? 32 : 92));
    EXPECT(55, deathboobs_storage_close(&storage) == CORE_OK);
    EXPECT(49, deathboobs_transaction_open(&path, NULL, &storage) == CORE_OK);
    EXPECT(50, page_is(storage, 1, 31) && page_is(storage, 2, 32));
    EXPECT(51, deathboobs_transaction_recover(storage, NULL) == CORE_OK);
    EXPECT(52, deathboobs_storage_close(&storage) == CORE_OK);
}
#endif

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;
    DeathboobsStorage *storage = NULL;
    DeathboobsStorage *other = NULL;
    DeathboobsTransaction *tx = NULL;
    DeathboobsTransaction *concurrent = NULL;
    DeathboobsPage one = make_page(1, 11);
    DeathboobsPage two = make_page(2, 12);
    DeathboobsPageId id;

    ErrorCode error = file_remove(&path);
    EXPECT(1, error == CORE_OK || error == CORE_NOT_FOUND);
    error = file_remove(&journal);
    EXPECT(2, error == CORE_OK || error == CORE_NOT_FOUND);
    EXPECT(3, deathboobs_transaction_open(&path, &journal, &storage) == CORE_OK);
    EXPECT(4, deathboobs_storage_open(&path, &other) == CORE_WOULD_BLOCK && !other);
#ifdef DEATHBOOBS_NATIVE_TEST
    test_process_lock();
#endif

    EXPECT(5, deathboobs_storage_page_append(storage, &one, &id) == CORE_OK && id == 1);
    EXPECT(6, deathboobs_storage_page_append(storage, &two, &id) == CORE_OK && id == 2);
    EXPECT(7, deathboobs_transaction_begin(storage, &path, &tx) == CORE_INVALID_ARGUMENT);
    EXPECT(8, deathboobs_transaction_begin(storage, &journal, &tx) == CORE_OK);
    EXPECT(9, deathboobs_transaction_begin(storage, NULL, &concurrent) == CORE_WOULD_BLOCK);
    EXPECT(10, deathboobs_transaction_recover(storage, NULL) == CORE_WOULD_BLOCK);
    one = make_page(1, 21);
    EXPECT(11, deathboobs_transaction_page_write(tx, &one) == CORE_OK);
    EXPECT(12, page_is(storage, 1, 11));
    EXPECT(13, deathboobs_transaction_rollback(&tx) == CORE_OK && !tx);
    EXPECT(14, page_is(storage, 1, 11) && page_is(storage, 2, 12));

    EXPECT(15, deathboobs_transaction_begin(storage, NULL, &tx) == CORE_OK);
    EXPECT(16, deathboobs_transaction_commit(&tx) == CORE_OK && !tx);
    EXPECT(17, deathboobs_transaction_begin(storage, NULL, &tx) == CORE_OK);
    DeathboobsPage bad = make_page(3, 1);
    EXPECT(18, deathboobs_transaction_page_write(tx, &bad) == CORE_NOT_FOUND);
    bad.id = 2;
    bad.payload_size = DEATHBOOBS_PAGE_PAYLOAD_SIZE + 1;
    EXPECT(19, deathboobs_transaction_page_write(tx, &bad) == CORE_INVALID_ARGUMENT);
    two = make_page(2, 32);
    EXPECT(20, deathboobs_transaction_page_write(tx, &two) == CORE_OK);
    EXPECT(21, deathboobs_transaction_page_write(tx, &one) == CORE_OK);
    one = make_page(1, 31);
    EXPECT(22, deathboobs_transaction_page_write(tx, &one) == CORE_OK);
    EXPECT(23, deathboobs_transaction_commit(&tx) == CORE_OK && !tx);
    EXPECT(24, deathboobs_storage_close(&storage) == CORE_OK);
    EXPECT(25, deathboobs_transaction_open(&path, NULL, &storage) == CORE_OK);
    EXPECT(26, page_is(storage, 1, 31) && page_is(storage, 2, 32));
    EXPECT(27, deathboobs_storage_close(&storage) == CORE_OK);

#ifdef DEATHBOOBS_NATIVE_TEST
    test_crashed_process(AFTER_FIRST_WRITE);
    test_crashed_process(BEFORE_UNLINK);
#endif
    EXPECT(28, file_remove(&path) == CORE_OK);
    EXPECT(29, file_remove(&journal) == CORE_NOT_FOUND);
    check_passed();
    return 0;
}
