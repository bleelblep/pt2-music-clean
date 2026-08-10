var Clay = require('@rebble/clay');
var clayConfig = require('./config.json');

var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

function parseIntValue(value, fallback) {
  var n = parseInt(value, 10);
  return isNaN(n) ? fallback : n;
}

function sendClaySettings(response) {
  console.log('[Clay] webviewclosed raw response: ' + response);
  var settings = clay.getSettings(response);
  console.log('[Clay] parsed settings payload: ' + JSON.stringify(settings));
  Pebble.sendAppMessage(settings, function() {
    console.log('[Clay] Sent config data to Pebble');
  }, function(error) {
    console.log('[Clay] Failed to send config data!');
    console.log('[Clay] sendAppMessage error: ' + JSON.stringify(error));
  });
}

Pebble.addEventListener('ready', function() {});

Pebble.addEventListener('appmessage', function(e) {
  var payload = e && e.payload ? e.payload : {};
  console.log('[Clay] inbound appmessage: ' + JSON.stringify(payload));
  var state = {};

  if (payload.AUDIO_ROUTE !== undefined) {
    state.CONFIG_AUDIO_ROUTE = parseIntValue(payload.AUDIO_ROUTE, 0);
  }
  if (payload.CONFIG_WATCH_VOLUME !== undefined) {
    state.CONFIG_WATCH_VOLUME = parseIntValue(payload.CONFIG_WATCH_VOLUME, 50);
  }
  if (payload.VOLUME !== undefined) {
    state.CONFIG_PHONE_VOLUME = parseIntValue(payload.VOLUME, 50);
  }
  if (payload.CONFIG_INPUT_MODE !== undefined) {
    state.CONFIG_INPUT_MODE = parseIntValue(payload.CONFIG_INPUT_MODE, 0);
  }
  if (payload.CONFIG_SHOW_PROGRESS !== undefined) {
    state.CONFIG_SHOW_PROGRESS = parseIntValue(payload.CONFIG_SHOW_PROGRESS, 1) === 1;
  }
  if (payload.CONFIG_CACHE_ENABLED !== undefined || payload.CACHE_ENABLED !== undefined) {
    state.CONFIG_CACHE_ENABLED = parseIntValue(
      payload.CONFIG_CACHE_ENABLED !== undefined ? payload.CONFIG_CACHE_ENABLED : payload.CACHE_ENABLED,
      1
    ) === 1;
  }
  if (payload.CONFIG_CACHE_SIZE_MB !== undefined || payload.CACHE_SIZE_MB !== undefined) {
    state.CONFIG_CACHE_SIZE_MB = parseIntValue(
      payload.CONFIG_CACHE_SIZE_MB !== undefined ? payload.CONFIG_CACHE_SIZE_MB : payload.CACHE_SIZE_MB,
      250
    );
  }
  if (payload.CONFIG_COVER_ART_BG !== undefined) {
    state.CONFIG_COVER_ART_BG = parseIntValue(payload.CONFIG_COVER_ART_BG, 0) === 1;
  }

  if (Object.keys(state).length > 0) {
    console.log('[Clay] applying state into Clay local settings: ' + JSON.stringify(state));
    clay.setSettings(state);
  }
});

Pebble.addEventListener('showConfiguration', function() {
  console.log('[Clay] showConfiguration, opening Clay URL');
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response) return;
  sendClaySettings(e.response);
});
