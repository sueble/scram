####################
SCRAM Coverity Check
####################

.. image:: https://travis-ci.org/rakhimov/scram.svg?branch=coverity-scan
    :target: https://travis-ci.org/rakhimov/scram
.. image:: https://scan.coverity.com/projects/2555/badge.svg
    :target: https://scan.coverity.com/projects/2555

This branch automates the submission of source and test files to Coverity.
The process is automated thanks to Travis-CI and Coverity.

In order to submit files, this branch must be merged with the *develop* branch,
and all unnecessary documentation, input, and shared non-source-code files
should be deleted. They are not needed for the submission to Coverity.
After updating the source files, Travis-CI adjustments may be made
in *.travis.yml* if necessary.

Then, the changes must be committed and pushed to the GitHub, which is picked
up by Travis-CI, where all building and submission happen.

In order to get more detailed information, follow the badge links at the top
of this file.
