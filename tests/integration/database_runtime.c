#define CHECK_TEST_NAME "deathboobs-database-runtime"
#include <testing/check.h>

#include <deathboobs/database.h>
#include <sys/file.h>

#define TEST_DB "/tmp/deathboobs_database_runtime.db"
#define TEST_JOURNAL TEST_DB ".journal"

static String path = {TEST_DB, sizeof(TEST_DB) - 1};
static String journal = {TEST_JOURNAL, sizeof(TEST_JOURNAL) - 1};

typedef struct VisitState {
    usize count;
    DeathboobsDetectionId id;
} VisitState;

static ErrorCode collect(const DeathboobsDetection *record, void *context)
{
    VisitState *state = context;
    ++state->count;
    state->id = record->id;
    return CORE_OK;
}

int main(void)
{
    DeathboobsDatabase *database = NULL;
    DeathboobsGameSession session = {0};
    DeathboobsGameSession loaded_session = {0};
    DeathboobsDetection detection = {0};
    DeathboobsDetection loaded_detection = {0};
    DeathboobsDetectionInput input = {0};
    VisitState visit = {0};
    ErrorCode error = file_remove(&path);
    EXPECT(1, error == CORE_OK || error == CORE_NOT_FOUND);
    error = file_remove(&journal);
    EXPECT(2, error == CORE_OK || error == CORE_NOT_FOUND);

    EXPECT(3, deathboobs_database_open(&path, &database) == CORE_OK);
    EXPECT(4, deathboobs_database_start_session(database, 1000, &session) == CORE_OK);
    EXPECT(5, session.id == 1 && session.status == DEATHBOOBS_SESSION_STATUS_ACTIVE);
    input.session_id = session.id;
    input.timestamp = 1250;
    input.has_target_id = true;
    input.target_id = 7;
    input.x = -1200;
    input.y = 3400;
    input.azimuth = 9000;
    input.distance = 3606;
    input.confidence = 9750;
    EXPECT(6, deathboobs_database_save_detection(database, &input, &detection) == CORE_OK);
    EXPECT(7, detection.id == 1);
    EXPECT(8, deathboobs_database_finish_session(database, session.id, 1300) == CORE_OK);
    EXPECT(9, deathboobs_database_close(&database) == CORE_OK && !database);

    EXPECT(10, deathboobs_database_open(&path, &database) == CORE_OK);
    EXPECT(11, deathboobs_database_get_session(database, 1, &loaded_session) == CORE_OK);
    EXPECT(12, loaded_session.status == DEATHBOOBS_SESSION_STATUS_FINISHED &&
               loaded_session.finished_at == 1300);
    EXPECT(13, deathboobs_database_get_detection(database, 1, &loaded_detection) == CORE_OK);
    EXPECT(14, loaded_detection.session_id == 1 && loaded_detection.target_id == 7 &&
               loaded_detection.azimuth == 9000 && loaded_detection.distance == 3606);
    EXPECT(15, deathboobs_database_visit_session_history(database, 1, collect, &visit) == CORE_OK);
    EXPECT(16, visit.count == 1 && visit.id == 1);
    EXPECT(17, deathboobs_database_close(&database) == CORE_OK && !database);
    EXPECT(18, file_remove(&path) == CORE_OK);
    EXPECT(19, file_remove(&journal) == CORE_NOT_FOUND);
    check_passed();
    return 0;
}
