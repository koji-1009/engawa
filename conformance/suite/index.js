'use strict';
// The conformance suite: import every test module (each self-registers via
// harness.test), then export the accumulated list for the runner.

require('./echo.test');
require('./capabilities.test');
require('./path.test');
require('./fs.test');

module.exports = require('../harness').tests;
