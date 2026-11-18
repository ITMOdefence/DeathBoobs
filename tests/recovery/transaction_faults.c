#define CHECK_TEST_NAME "deathboobs-transaction-faults"
#include <testing/check.h>

#include <deathboobs/transaction.h>
#include <sys/file.h>

#include "../fakes/punos_faults.h"
#include "../../src/internal/page_codec.h"

#define DB "/tmp/deathboobs_fault.db"
#define FOREIGN "/tmp/deathboobs_foreign.db"
#define JOURNAL DB ".journal"
#define STAGE JOURNAL ".tmp"
#define DIR "/tmp/"
#define HEADER_SIZE 64
#define STR(literal) {literal, sizeof(literal) - 1}

static const String database = STR(DB);
static const String journal = STR(JOURNAL);
static const String stage = STR(STAGE);
static const String foreign = STR(FOREIGN);
static const String foreign_journal = STR(FOREIGN ".journal");

static DeathboobsPage page_at(u64 id, u8 value)
{
    return (DeathboobsPage){
        .id = id, .type = DEATHBOOBS_PAGE_TYPE_SESSIONS, .generation = value,
        .flags = value, .payload_size = 1, .payload = {value}
    };
}

static bool page_is(DeathboobsStorage *storage, u64 id, u8 value)
{
    DeathboobsPage page;
    return deathboobs_storage_page_read(storage, id, &page) == CORE_OK &&
           page.payload_size == 1 && page.generation == value &&
           page.flags == value && page.payload[0] == value;
}

static DeathboobsStorage *new_database(const String *path)
{
    DeathboobsStorage *storage = NULL;
    DeathboobsPage first = page_at(1, 11);
    DeathboobsPage second = page_at(2, 12);
    DeathboobsPageId id;
    EXPECT(1, deathboobs_transaction_open(path, NULL, &storage) == CORE_OK);
    EXPECT(2, deathboobs_storage_page_append(storage, &first, &id) == CORE_OK && id == 1);
    EXPECT(3, deathboobs_storage_page_append(storage, &second, &id) == CORE_OK && id == 2);
    return storage;
}

static DeathboobsTransaction *stage_writes(DeathboobsStorage *storage)
{
    DeathboobsTransaction *tx = NULL;
    DeathboobsPage first = page_at(1, 21);
    DeathboobsPage second = page_at(2, 32);
    EXPECT(4, deathboobs_transaction_begin(storage, NULL, &tx) == CORE_OK);
    EXPECT(5, deathboobs_transaction_page_write(tx, &second) == CORE_OK);
    EXPECT(6, deathboobs_transaction_page_write(tx, &first) == CORE_OK);
    first = page_at(1, 31);
    EXPECT(7, deathboobs_transaction_page_write(tx, &first) == CORE_OK);
    return tx;
}

static void expect_absent(const String *path)
{
    FileHandle file = NULL;
    EXPECT(8, file_open(path, FILE_OPEN_MODE_READ, 0, &file) == CORE_NOT_FOUND);
}

typedef struct CommitFault {
    FakeOperation operation;
    const char *path;
    usize nth;
    usize partial;
    bool commit_cleanup;
    bool no_db_changes;
} CommitFault;

static void test_commit_fault(CommitFault fault, bool durable)
{
    DeathboobsStorage *storage = new_database(&database);
    DeathboobsTransaction *tx = stage_writes(storage);

    fake_fail(fault.operation, fault.path, fault.nth, CORE_TIMEOUT, fault.partial);
    EXPECT(10, deathboobs_transaction_commit(&tx) == CORE_TIMEOUT && tx);
    if (fault.no_db_changes) {
        EXPECT(11, page_is(storage, 1, 11) && page_is(storage, 2, 12));
    }
    fake_capture_disk(durable);

    if (fault.commit_cleanup) {
        EXPECT(12, deathboobs_transaction_rollback(&tx) == CORE_INVALID_STATE);
        EXPECT(13, deathboobs_transaction_commit(&tx) == CORE_OK && !tx);
        EXPECT(14, page_is(storage, 1, 31) && page_is(storage, 2, 32));
    } else {
        EXPECT(15, deathboobs_transaction_rollback(&tx) == CORE_OK && !tx);
        EXPECT(16, page_is(storage, 1, 11) && page_is(storage, 2, 12));
    }
    EXPECT(17, deathboobs_storage_close(&storage) == CORE_OK);
    EXPECT(18, fake_no_resources());

    fake_restore_disk();

    EXPECT(19, deathboobs_transaction_open(&database, NULL, &storage) == CORE_OK);
    bool new_state = !durable && fault.commit_cleanup && fault.operation == FAKE_FLUSH;
    EXPECT(20, page_is(storage, 1, new_state ? 31 : 11));
    EXPECT(21, page_is(storage, 2, new_state ? 32 : 12));
    EXPECT(22, deathboobs_transaction_recover(storage, NULL) == CORE_OK);
    expect_absent(&stage);
    expect_absent(&journal);
    EXPECT(23, deathboobs_storage_close(&storage) == CORE_OK);
    EXPECT(24, file_remove(&database) == CORE_OK);
    EXPECT(25, fake_no_resources());
}

static void test_commit_fault_matrix(void)
{
    const CommitFault faults[] = {
        {FAKE_OPEN,   STAGE,   1,  0, false, true},
        {FAKE_OPEN,   STAGE,   1,  1, false, true},
        {FAKE_WRITE,  STAGE,   1,  7, false, true},
        {FAKE_WRITE,  STAGE,   2, 31, false, true},
        {FAKE_WRITE,  STAGE,   3, 33, false, true},
        {FAKE_FLUSH,  STAGE,   1,  0, false, true},
        {FAKE_CLOSE,  STAGE,   1,  0, false, true},
        {FAKE_RENAME, STAGE,   1,  0, false, true},
        {FAKE_FLUSH,  DIR,     1,  0, false, true},
        {FAKE_WRITE,  DB,      2, 24, false, false},
        {FAKE_FLUSH,  DB,      1,  0, false, false},
        {FAKE_REMOVE, JOURNAL, 1,  0, true,  false},
        {FAKE_FLUSH,  DIR,     2,  0, true,  false}
    };
    for (usize i = 0; i < sizeof(faults) / sizeof(faults[0]); ++i) {
        test_commit_fault(faults[i], false);
        test_commit_fault(faults[i], true);
    }
}

static void leave_published_journal(void)
{
    DeathboobsStorage *storage = new_database(&database);
    DeathboobsTransaction *tx = stage_writes(storage);
    fake_fail(FAKE_WRITE, DB, 2, CORE_TIMEOUT, 24);
    EXPECT(30, deathboobs_transaction_commit(&tx) == CORE_TIMEOUT);
    fake_capture_disk(false);
    EXPECT(31, deathboobs_transaction_rollback(&tx) == CORE_OK);
    EXPECT(32, deathboobs_storage_close(&storage) == CORE_OK);
    fake_restore_disk();
}

static void test_recovery_retry(FakeOperation op, const char *path, usize nth)
{
    DeathboobsStorage *storage = NULL;
    leave_published_journal();
    fake_fail(op, path, nth, CORE_TIMEOUT, 24);
    EXPECT(33, deathboobs_transaction_open(&database, NULL, &storage) == CORE_TIMEOUT);
    EXPECT(34, !storage && fake_no_resources());
    EXPECT(35, deathboobs_transaction_open(&database, NULL, &storage) == CORE_OK);
    EXPECT(36, page_is(storage, 1, 11) && page_is(storage, 2, 12));
    EXPECT(37, deathboobs_transaction_recover(storage, NULL) == CORE_OK);
    expect_absent(&journal);
    EXPECT(38, deathboobs_storage_close(&storage) == CORE_OK);
    EXPECT(39, file_remove(&database) == CORE_OK && fake_no_resources());
}

static FileHandle open_journal_for_edit(void)
{
    FileHandle file = NULL;
    FileInfo info;
    EXPECT(40, file_open(&journal, FILE_OPEN_MODE_READ_WRITE_CREATE, 0, &file) == CORE_OK);
    EXPECT(41, file_info(file, &info) == CORE_OK);
    /* Two distinct pages were logged although page 1 was staged twice. */
    EXPECT(42, info.size == HEADER_SIZE + 2 * DEATHBOOBS_PAGE_SIZE);
    return file;
}

static void overwrite(FileHandle file, i64 offset, Bytes bytes)
{
    u64 position;
    EXPECT(43, file_seek(file, offset, FILE_SEEK_ORIGIN_START, &position) == CORE_OK);
    EXPECT(44, position == (u64)offset && file_write_all(file, bytes) == CORE_OK);
}

typedef enum Damage {
    BAD_HEADER_CRC,
    BAD_RECORD_CRC,
    TRUNCATED_RECORD,
    UNKNOWN_VERSION,
    OVERFLOWED_RECORD_COUNT
} Damage;

static void damage_journal(Damage damage)
{
    FileHandle file = open_journal_for_edit();
    u8 header[HEADER_SIZE];
    usize count;
    u8 byte = 0xff;

    switch (damage) {
    case BAD_HEADER_CRC:
        overwrite(file, 60, (Bytes){&byte, 1});
        break;
    case BAD_RECORD_CRC:
        overwrite(file, HEADER_SIZE + DEATHBOOBS_PAGE_SIZE + 32, (Bytes){&byte, 1});
        break;
    case TRUNCATED_RECORD:
        fake_truncate(JOURNAL, HEADER_SIZE + 2 * DEATHBOOBS_PAGE_SIZE - 1);
        break;
    case UNKNOWN_VERSION:
    case OVERFLOWED_RECORD_COUNT:
        EXPECT(45, file_read(file, (Bytes){header, sizeof(header)}, &count) == CORE_OK);
        EXPECT(46, count == sizeof(header));
        if (damage == UNKNOWN_VERSION) {
            put_u16_le(header + 8, 2);
        } else {
            put_u64_le(header + 40, USIZE_MAX);
        }
        put_u32_le(header + 56, 0);
        put_u32_le(header + 56, crc32(header, sizeof(header)));
        overwrite(file, 0, (Bytes){header, sizeof(header)});
        break;
    }
    EXPECT(47, file_close(&file) == CORE_OK);
}

static void test_invalid_journal(Damage damage, ErrorCode expected)
{
    DeathboobsStorage *storage = NULL;
    leave_published_journal();
    damage_journal(damage);
    EXPECT(48, deathboobs_transaction_open(&database, NULL, &storage) == expected);
    EXPECT(49, !storage && fake_no_resources());
    EXPECT(50, deathboobs_storage_open(&database, &storage) == CORE_OK);
    EXPECT(51, page_is(storage, 1, 31));
    EXPECT(52, deathboobs_storage_close(&storage) == CORE_OK);
    EXPECT(53, file_remove(&journal) == CORE_OK);
    EXPECT(54, file_remove(&database) == CORE_OK);
    EXPECT(55, fake_no_resources());
}

static void test_foreign_journal(void)
{
    DeathboobsStorage *storage;
    leave_published_journal();
    storage = new_database(&foreign);
    EXPECT(60, deathboobs_storage_close(&storage) == CORE_OK);
    EXPECT(61, file_rename(&journal, &foreign_journal) == CORE_OK);
    EXPECT(62, deathboobs_transaction_open(&foreign, NULL, &storage) == CORE_INVALID_STATE);
    EXPECT(63, !storage && fake_no_resources());
    EXPECT(64, deathboobs_storage_open(&foreign, &storage) == CORE_OK);
    EXPECT(65, page_is(storage, 1, 11) && page_is(storage, 2, 12));
    EXPECT(66, deathboobs_storage_close(&storage) == CORE_OK);
    EXPECT(67, file_remove(&foreign_journal) == CORE_OK);
    EXPECT(68, file_remove(&foreign) == CORE_OK);
    EXPECT(69, file_remove(&database) == CORE_OK);
}

static void test_primary_and_cleanup_errors(void)
{
    DeathboobsStorage *storage = new_database(&database);
    DeathboobsTransaction *tx = stage_writes(storage);
    fake_fail(FAKE_WRITE, STAGE, 2, CORE_TIMEOUT, 33);
    fake_fail(FAKE_CLOSE, STAGE, 1, CORE_PERMISSION_DENIED, 0);
    EXPECT(70, deathboobs_transaction_commit(&tx) == CORE_TIMEOUT && tx);
    fake_fail(FAKE_REMOVE, STAGE, 1, CORE_PERMISSION_DENIED, 0);
    EXPECT(71, deathboobs_transaction_rollback(&tx) == CORE_PERMISSION_DENIED && tx);
    EXPECT(72, deathboobs_transaction_commit(&tx) == CORE_INVALID_STATE);
    EXPECT(73, deathboobs_transaction_rollback(&tx) == CORE_OK && !tx);
    EXPECT(74, page_is(storage, 1, 11) && page_is(storage, 2, 12));

    tx = stage_writes(storage);
    fake_fail(FAKE_WRITE, DB, 2, CORE_TIMEOUT, 24);
    EXPECT(75, deathboobs_transaction_commit(&tx) == CORE_TIMEOUT && tx);
    fake_fail(FAKE_WRITE, DB, 2, CORE_PERMISSION_DENIED, 24);
    EXPECT(76, deathboobs_transaction_rollback(&tx) == CORE_PERMISSION_DENIED && tx);
    EXPECT(77, deathboobs_transaction_rollback(&tx) == CORE_OK && !tx);
    EXPECT(78, page_is(storage, 1, 11) && page_is(storage, 2, 12));

    tx = stage_writes(storage);
    fake_fail(FAKE_FREE, NULL, 3, CORE_IO_ERROR, 0);
    EXPECT(79, deathboobs_transaction_commit(&tx) == CORE_IO_ERROR && tx);
    EXPECT(80, deathboobs_transaction_commit(&tx) == CORE_OK && !tx);
    EXPECT(81, page_is(storage, 1, 31) && page_is(storage, 2, 32));
    EXPECT(82, deathboobs_storage_close(&storage) == CORE_OK);
    EXPECT(83, file_remove(&database) == CORE_OK);
    EXPECT(84, fake_no_resources());

    leave_published_journal();
    fake_fail(FAKE_READ, JOURNAL, 2, CORE_TIMEOUT, 0);
    fake_fail(FAKE_CLOSE, JOURNAL, 1, CORE_PERMISSION_DENIED, 0);
    EXPECT(85, deathboobs_transaction_open(&database, NULL, &storage) == CORE_TIMEOUT);
    EXPECT(86, !storage && fake_no_resources());
    EXPECT(87, deathboobs_transaction_open(&database, NULL, &storage) == CORE_OK);
    EXPECT(88, page_is(storage, 1, 11) && page_is(storage, 2, 12));
    EXPECT(89, deathboobs_storage_close(&storage) == CORE_OK);
    EXPECT(90, file_remove(&database) == CORE_OK);
}

static void test_commit_retry(void)
{
    DeathboobsStorage *storage = new_database(&database);
    DeathboobsTransaction *tx = stage_writes(storage);
    fake_fail(FAKE_WRITE, STAGE, 2, CORE_TIMEOUT, 31);
    EXPECT(91, deathboobs_transaction_commit(&tx) == CORE_TIMEOUT && tx);
    fake_fail(FAKE_WRITE, DB, 2, CORE_TIMEOUT, 24);
    EXPECT(92, deathboobs_transaction_commit(&tx) == CORE_TIMEOUT && tx);
    EXPECT(93, deathboobs_transaction_commit(&tx) == CORE_OK && !tx);
    EXPECT(94, page_is(storage, 1, 31) && page_is(storage, 2, 32));
    fake_capture_disk(true);
    EXPECT(95, deathboobs_storage_close(&storage) == CORE_OK);
    fake_restore_disk();
    EXPECT(96, deathboobs_transaction_open(&database, NULL, &storage) == CORE_OK);
    EXPECT(97, page_is(storage, 1, 31) && page_is(storage, 2, 32));
    EXPECT(98, deathboobs_storage_close(&storage) == CORE_OK);
    EXPECT(99, file_remove(&database) == CORE_OK);
}

static void test_append_rollback_truncates(void)
{
    DeathboobsStorage *storage = new_database(&database);
    DeathboobsTransaction *tx = NULL;
    DeathboobsPage appended = page_at(0, 44);
    DeathboobsPage page;
    DeathboobsPageId id = 0;
    u64 count = 0;
    EXPECT(101, deathboobs_transaction_begin(storage, NULL, &tx) == CORE_OK);
    EXPECT(102, deathboobs_transaction_page_append(tx, &appended, &id) == CORE_OK &&
                id == 3);
    fake_fail(FAKE_WRITE, DB, 1, CORE_TIMEOUT, 24);
    EXPECT(103, deathboobs_transaction_commit(&tx) == CORE_TIMEOUT && tx);
    EXPECT(104, deathboobs_storage_page_count(storage, &count) == CORE_OK && count == 3);
    EXPECT(105, deathboobs_transaction_rollback(&tx) == CORE_OK && !tx);
    EXPECT(106, deathboobs_storage_page_count(storage, &count) == CORE_OK && count == 2);
    EXPECT(107, deathboobs_storage_page_read(storage, 3, &page) == CORE_NOT_FOUND);
    EXPECT(108, deathboobs_storage_close(&storage) == CORE_OK);
    EXPECT(109, file_remove(&database) == CORE_OK && fake_no_resources());
}

int main(void)
{
    test_commit_fault_matrix();
    test_recovery_retry(FAKE_READ, JOURNAL, 2);
    test_recovery_retry(FAKE_WRITE, DB, 2);
    test_recovery_retry(FAKE_FLUSH, DB, 1);
    test_recovery_retry(FAKE_CLOSE, JOURNAL, 1);
    test_recovery_retry(FAKE_REMOVE, JOURNAL, 1);
    test_recovery_retry(FAKE_FLUSH, DIR, 2);
    test_invalid_journal(BAD_HEADER_CRC, CORE_IO_ERROR);
    test_invalid_journal(BAD_RECORD_CRC, CORE_IO_ERROR);
    test_invalid_journal(TRUNCATED_RECORD, CORE_IO_ERROR);
    test_invalid_journal(UNKNOWN_VERSION, CORE_UNSUPPORTED);
    test_invalid_journal(OVERFLOWED_RECORD_COUNT, CORE_IO_ERROR);
    test_foreign_journal();
    test_primary_and_cleanup_errors();
    test_commit_retry();
    test_append_rollback_truncates();
    EXPECT(100, fake_no_resources());
    check_passed();
    return 0;
}
