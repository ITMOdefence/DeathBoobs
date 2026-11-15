#define CHECK_TEST_NAME "deathboobs-storage"
#include <testing/check.h>

#include <deathboobs/storage.h>
#include <sys/file.h>
#include <sys/string.h>

#define TEST_FILE "/tmp/deathboobs_storage_test.db"
#define BAD_HEADER_FILE "/tmp/deathboobs_storage_bad_header.db"
#define SHORT_FILE "/tmp/deathboobs_storage_short.db"

static String make_string(const char *text)
{
    String result;
    if (string_from_c_str(text, &result) != CORE_OK) {
        check_fail(250);
    }
    return result;
}

static void remove_if_present(const String *path)
{
    ErrorCode error = file_remove(path);
    if (error != CORE_OK && error != CORE_NOT_FOUND) {
        check_fail(251);
    }
}

static bool payload_is_zero(const DeathboobsPage *page, usize from)
{
    usize i;
    for (i = from; i < DEATHBOOBS_PAGE_PAYLOAD_SIZE; ++i) {
        if (page->payload[i] != 0) {
            return false;
        }
    }
    return true;
}

static void test_create_append_read_write_reopen(void)
{
    String path = make_string(TEST_FILE);
    DeathboobsStorage *storage = NULL;
    DeathboobsPage page = {0};
    DeathboobsPage read = {0};
    DeathboobsPageId id = 0;
    u64 count = 99;

    remove_if_present(&path);
    EXPECT(1, deathboobs_storage_open(&path, &storage) == CORE_OK);
    EXPECT(2, storage != NULL);
    EXPECT(3, deathboobs_storage_page_count(storage, &count) == CORE_OK && count == 0);
    EXPECT(4, deathboobs_storage_page_read(storage, 1, &read) == CORE_NOT_FOUND);

    page.type = DEATHBOOBS_PAGE_TYPE_DETECTIONS;
    page.flags = 7;
    page.generation = 11;
    page.payload_size = 5;
    page.payload[0] = 't';
    page.payload[1] = 'a';
    page.payload[2] = 'r';
    page.payload[3] = 'g';
    page.payload[4] = 't';
    EXPECT(5, deathboobs_storage_page_append(storage, &page, &id) == CORE_OK);
    EXPECT(6, id == 1 && page.id == 1);
    EXPECT(7, deathboobs_storage_page_count(storage, &count) == CORE_OK && count == 1);
    EXPECT(8, deathboobs_storage_page_read(storage, 1, &read) == CORE_OK);
    EXPECT(9, read.id == 1 && read.type == DEATHBOOBS_PAGE_TYPE_DETECTIONS &&
                  read.flags == 7 && read.generation == 11 && read.payload_size == 5);
    EXPECT(10, read.payload[0] == 't' && read.payload[4] == 't' &&
                   payload_is_zero(&read, 5));

    page.generation = 12;
    page.payload_size = 2;
    page.payload[0] = 'o';
    page.payload[1] = 'k';
    EXPECT(11, deathboobs_storage_page_write(storage, &page) == CORE_OK);
    EXPECT(12, deathboobs_storage_flush(storage) == CORE_OK);
    EXPECT(13, deathboobs_storage_close(&storage) == CORE_OK && storage == NULL);

    EXPECT(14, deathboobs_storage_open(&path, &storage) == CORE_OK);
    EXPECT(15, deathboobs_storage_page_count(storage, &count) == CORE_OK && count == 1);
    EXPECT(16, deathboobs_storage_page_read(storage, 1, &read) == CORE_OK);
    EXPECT(17, read.generation == 12 && read.payload_size == 2 &&
                   read.payload[0] == 'o' && read.payload[1] == 'k' &&
                   payload_is_zero(&read, 2));
    EXPECT(18, deathboobs_storage_close(&storage) == CORE_OK);

    remove_if_present(&path);
    (void)string_destroy(&path);
}

static void test_arguments_and_invalid_pages(void)
{
    String path = make_string(TEST_FILE);
    DeathboobsStorage *storage = NULL;
    DeathboobsPage page = {0};
    DeathboobsPageId id = 0;
    u64 count;

    remove_if_present(&path);
    EXPECT(30, deathboobs_storage_open(NULL, &storage) == CORE_INVALID_ARGUMENT);
    EXPECT(31, deathboobs_storage_open(&path, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(32, deathboobs_storage_open(&path, &storage) == CORE_OK);
    EXPECT(33, deathboobs_storage_page_count(NULL, &count) == CORE_INVALID_ARGUMENT);
    EXPECT(34, deathboobs_storage_page_count(storage, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(35, deathboobs_storage_page_append(NULL, &page, &id) == CORE_INVALID_ARGUMENT);
    EXPECT(36, deathboobs_storage_page_append(storage, NULL, &id) == CORE_INVALID_ARGUMENT);
    EXPECT(37, deathboobs_storage_page_append(storage, &page, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(38, deathboobs_storage_page_append(storage, &page, &id) == CORE_INVALID_ARGUMENT);

    page.type = DEATHBOOBS_PAGE_TYPE_SESSIONS;
    page.payload_size = (u32)DEATHBOOBS_PAGE_PAYLOAD_SIZE + 1;
    EXPECT(39, deathboobs_storage_page_append(storage, &page, &id) == CORE_INVALID_ARGUMENT);
    page.payload_size = 0;
    EXPECT(40, deathboobs_storage_page_write(storage, &page) == CORE_NOT_FOUND);
    EXPECT(41, deathboobs_storage_page_read(storage, 0, &page) == CORE_NOT_FOUND);
    EXPECT(42, deathboobs_storage_page_read(storage, 1, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(43, deathboobs_storage_flush(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(44, deathboobs_storage_close(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(45, deathboobs_storage_close(&storage) == CORE_OK);
    EXPECT(46, deathboobs_storage_close(&storage) == CORE_INVALID_ARGUMENT);

    remove_if_present(&path);
    (void)string_destroy(&path);
}

static void test_corruption_detection(void)
{
    String path = make_string(TEST_FILE);
    DeathboobsStorage *storage = NULL;
    DeathboobsPage page = {0};
    DeathboobsPageId id;
    FileHandle file = NULL;
    u64 position;
    u8 bad = 0xff;

    remove_if_present(&path);
    EXPECT(60, deathboobs_storage_open(&path, &storage) == CORE_OK);
    page.type = DEATHBOOBS_PAGE_TYPE_METADATA;
    page.payload_size = 1;
    page.payload[0] = 42;
    EXPECT(61, deathboobs_storage_page_append(storage, &page, &id) == CORE_OK);
    EXPECT(62, deathboobs_storage_close(&storage) == CORE_OK);

    EXPECT(63, file_open(&path, FILE_OPEN_MODE_READ_WRITE_CREATE,
                         FILE_DEFAULT_PERMISSIONS, &file) == CORE_OK);
    EXPECT(64, file_seek(file, (i64)DEATHBOOBS_PAGE_SIZE + 32,
                         FILE_SEEK_ORIGIN_START, &position) == CORE_OK);
    EXPECT(65, file_write_all(file, (Bytes){&bad, 1}) == CORE_OK);
    EXPECT(66, file_close(&file) == CORE_OK);

    EXPECT(67, deathboobs_storage_open(&path, &storage) == CORE_OK);
    EXPECT(68, deathboobs_storage_page_read(storage, 1, &page) == CORE_IO_ERROR);
    EXPECT(69, deathboobs_storage_close(&storage) == CORE_OK);
    remove_if_present(&path);
    (void)string_destroy(&path);
}

static void test_invalid_files(void)
{
    String bad_header = make_string(BAD_HEADER_FILE);
    String short_file = make_string(SHORT_FILE);
    DeathboobsStorage *storage = NULL;
    FileHandle file = NULL;
    u8 bad_page[DEATHBOOBS_PAGE_SIZE] = {0};
    u8 one = 1;

    remove_if_present(&bad_header);
    remove_if_present(&short_file);
    EXPECT(80, file_open(&bad_header, FILE_OPEN_MODE_WRITE_TRUNCATE,
                         FILE_DEFAULT_PERMISSIONS, &file) == CORE_OK);
    EXPECT(81, file_write_all(file, (Bytes){bad_page, sizeof(bad_page)}) == CORE_OK);
    EXPECT(82, file_close(&file) == CORE_OK);
    EXPECT(83, deathboobs_storage_open(&bad_header, &storage) == CORE_IO_ERROR &&
                   storage == NULL);

    EXPECT(84, file_open(&short_file, FILE_OPEN_MODE_WRITE_TRUNCATE,
                         FILE_DEFAULT_PERMISSIONS, &file) == CORE_OK);
    EXPECT(85, file_write_all(file, (Bytes){&one, 1}) == CORE_OK);
    EXPECT(86, file_close(&file) == CORE_OK);
    EXPECT(87, deathboobs_storage_open(&short_file, &storage) == CORE_IO_ERROR &&
                   storage == NULL);

    remove_if_present(&bad_header);
    remove_if_present(&short_file);
    (void)string_destroy(&bad_header);
    (void)string_destroy(&short_file);
}

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;
    test_create_append_read_write_reopen();
    test_arguments_and_invalid_pages();
    test_corruption_detection();
    test_invalid_files();
    check_passed();
    return 0;
}
