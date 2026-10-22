# Feature fragment: track colours (features/track_colours.feature). A track's
# colour is saved by its own record (src/project/records_colour.cpp) and drawn
# on its clips, its arrangement header and its mixer strip.

target_sources(blokkily_core PRIVATE src/project/records_colour.cpp)
