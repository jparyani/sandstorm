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

module.exports = {};


module.exports["Test install" ] = function (browser) {
  browser
    .url(this.launchUrl + "/demo")
    .execute('window.Meteor.logout()')
    .pause(short_wait)
    .waitForElementVisible('.start', medium_wait)
    .click(".start")
    .waitForElementVisible('.app-list', medium_wait)
    .resizeWindow(utils.default_width, utils.default_height)
    .url(browser.launch_url + "/install/ca690ad886bf920026f8b876c19539c1?url=http://sandstorm.io/apps/ssjekyll8.spk")
    .waitForElementVisible('#step-confirm', very_long_wait)
    .click('#confirmInstall')
    .waitForElementVisible('.app-action[data-app-id="nqmcqs9spcdpmqyuxemf0tsgwn8awfvswc58wgk375g4u25xv6yh"]', medium_wait)
    .assert.containsText('.app-action[data-app-id="nqmcqs9spcdpmqyuxemf0tsgwn8awfvswc58wgk375g4u25xv6yh"]>.app-title', 'Hacker CMS');
};

module.exports["Test new grain" ] = function (browser) {
  browser
    .url(browser.launch_url + "/grain/new")
    .waitForElementVisible('.app-list', medium_wait)
    .waitForElementVisible('.app-action[data-app-id="nqmcqs9spcdpmqyuxemf0tsgwn8awfvswc58wgk375g4u25xv6yh"]', medium_wait)
    .click('.app-action[data-app-id="nqmcqs9spcdpmqyuxemf0tsgwn8awfvswc58wgk375g4u25xv6yh"]')
    .waitForElementVisible('#grainTitle', medium_wait)
    .assert.containsText('#grainTitle', 'Untitled Hacker CMS Site');
};

module.exports["Test grain frame" ] = function (browser) {
  browser
    .pause(short_wait)
    .frame('grain-frame')
    .waitForElementPresent('#publish', medium_wait)
    .assert.containsText('#publish', 'Publish')
    .frame(null)
    .waitForElementVisible(".delete", short_wait)
    .click(".delete")
    .pause(1000);
};
