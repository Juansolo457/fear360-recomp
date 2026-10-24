# The shader warm-up list a build compiles in (src/shader_warmup.h, #53), run at every build:
#   cmake -DSRC=<project>/warmup/shader_warmup.inc -DDST=<build>/warmup/shader_warmup.inc -P warmup_list.cmake
#
# The list is generated locally by tools/make_warmup.py from the player's own game files and
# shader caches, and is not tracked (.gitignore). DST gets that list when SRC exists, else an
# empty one: the build works either way, and without a list the renderer still builds what its
# own cache has seen before play. DST is only rewritten when its content changes, so the source
# that includes it rebuilds exactly when a list appears, changes or goes away.

if(EXISTS "${SRC}")
  set(content_file "${SRC}")
else()
  set(content_file "${DST}.empty")
  file(WRITE "${content_file}"
       "// No shader warm-up list: tools/make_warmup.py writes one (src/shader_warmup.h).\n"
       "R\"WARMUP()WARMUP\"\n")
endif()
get_filename_component(dst_dir "${DST}" DIRECTORY)
file(MAKE_DIRECTORY "${dst_dir}")
file(COPY_FILE "${content_file}" "${DST}" ONLY_IF_DIFFERENT)
