# Single source of truth for the SsdReady version.
#
# Final version: MAJOR.MINOR.BUILD (+ short git hash), where
#   BUILD = SSDREADY_BUILD_OFFSET + number of commits reachable from HEAD.
#
# Bump MAJOR/MINOR here for a release; BUILD is computed automatically.
set(SSDREADY_VERSION_MAJOR 1)
set(SSDREADY_VERSION_MINOR 40)

# Builds made with the old AutoBuild counter ended at 367. The offset keeps
# build numbers increasing. Do not lower it.
set(SSDREADY_BUILD_OFFSET 367)
