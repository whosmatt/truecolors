# Host (linux-target) unit tests for the pure-math modules. The hardware
# components they live in don't build for linux, so the sources under test
# are compiled directly.
set(REPO ../../..)
idf_component_register(
    SRCS "test_runner.c"
         "${REPO}/components/laser/laser_math.c"
         "${REPO}/components/laser/test/test_laser_math.c"
         "${REPO}/components/sysmgr/sysmgr_safety.c"
         "${REPO}/components/sysmgr/test/test_sysmgr_safety.c"
    PRIV_INCLUDE_DIRS "${REPO}/components/laser/include"
                      "${REPO}/components/sysmgr/include"
                      "${REPO}/components/common/include"
    PRIV_REQUIRES unity
    WHOLE_ARCHIVE   # keep the auto-registered TEST_CASE objects in the link
)

target_link_libraries(${COMPONENT_LIB} PRIVATE m)
