add_executable(s3re_launcher launcher.cpp)

target_link_libraries(s3re_launcher PRIVATE detours bcrypt)
target_include_directories(s3re_launcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(s3re_launcher PRIVATE
    UNICODE _UNICODE
    S3RE_DEFAULT_GAME_DIR=L"${S3RE_GAME_DIR}"
)
target_compile_options(s3re_launcher PRIVATE /W4 /utf-8)

# The launcher is useless without the DLL; always build them together.
add_dependencies(s3re_launcher s3re)
