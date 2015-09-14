// Sandstorm - Personal Cloud Sandbox
// Copyright (c) 2014 Sandstorm Development Group, Inc. and contributors
// All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

'use strict';

var utils = require('../utils'),
    short_wait = utils.short_wait,
    medium_wait = utils.medium_wait,
    long_wait = utils.long_wait,
    very_long_wait = utils.very_long_wait;
var path = require('path');
var assetsPath = path.resolve(__dirname, '../assets');

var replica_id = process.env.OASIS_REPLICA_ID || 0;

module.exports = {};

module.exports["Init cookie " + replica_id] = function (browser) {
  browser
    .url("https://sandstorm.io")
    .execute("document.cookie = 'force_replica=" + replica_id + ";domain=.sandstorm.io;expires=Sat, 01-Jan-2030 00:00:00 GMT';")
    .pause(1000);
};

module.exports["Test shared grain " + replica_id] = function (browser) {
  browser
    .url(browser.launch_url + "/shared/1ilQmtlp4cUM4rwuHKAFTwC_3k9SiwGAtJRxXXSHPVx")
    .resizeWindow(utils.default_width, utils.default_height)
    .waitForElementVisible('#grainTitle', medium_wait)
    .assert.containsText('#grainTitle', 'Monitoring Test Grain');
};

module.exports["Test grain frame " + replica_id] = function (browser) {
  browser
    .waitForElementVisible('#grain-frame', medium_wait)
    .frame('grain-frame')
    .waitForElementPresent('#publish', medium_wait)
    .assert.containsText('#publish', 'Publish');
};
