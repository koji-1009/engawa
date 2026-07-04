'use strict';
// The conformance suite: import every test module (each self-registers via
// harness.test), then export the accumulated list for the runner.

require('./echo.test');
require('./capabilities.test');
require('./csp.test');
require('./path.test');
require('./fs.test');
require('./app.test');
require('./clipboard.test');
require('./window.test');
require('./shellOpen.test');
require('./notification.test');
require('./process.test');
require('./dialog.test');

module.exports = require('../harness').tests;
