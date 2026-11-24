# States from earlier versions

Each file is a state saved by a released version of plumnin with its `plumnin-headless`,
after frame 89 of the test pattern made to look like one console:

    plumnin-headless --test-rom --system SYSTEM --frames 90 --save-state VERSION-SYSTEM.state

The `headless` test (`tests/headless_tests.cmake`) loads each one with the current
version and checks it carries on exactly like that version's run that never stopped
(`carry_on.txt`, frame 119). The test pattern does not run the consoles' own chips, so
this checks the state's container (header, blocks, thumbnail) and not each console's
blocks. If it fails, the states people have already saved would stop loading.

When releasing a version, add its states.
