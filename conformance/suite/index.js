'use strict';
// The conformance suite: import every test module (each self-registers via
// harness.test), then export the accumulated list for the runner.

require('./echo.test');
require('./capabilities.test');

module.exports = require('../harness').tests;
