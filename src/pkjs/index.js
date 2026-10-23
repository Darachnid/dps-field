// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Darachnid

var FEED = "http://127.0.0.1:4774/dps";
var POLL_MS = 5000;

var outbox = [];
var sending = false;
var busy = false;
var pollTimer = null;

function enqueue(payload) {
  outbox.push(payload);
  pump();
}

function pump() {
  if (sending || !outbox.length) return;
  sending = true;
  var item = outbox.shift();
  Pebble.sendAppMessage(item, function() {
    sending = false;
    pump();
  }, function() {
    sending = false;
    console.log("dps send failed");
    pump();
  });
}

function push(kind, body) {
  enqueue({ KIND: kind, BODY: body || "" });
}

function publish(data) {
  push("sum", data && data.sum ? data.sum : "");
  push("nodes", data && data.nodes ? data.nodes : "");
  push("log", data && data.log ? data.log : "");
}

function getText(url, ms, ok, fail) {
  var done = false;
  var req = new XMLHttpRequest();
  var timer = setTimeout(function() {
    if (done) return;
    done = true;
    try { req.abort(); } catch (ignore) {}
    fail("timeout");
  }, ms);
  try {
    req.open("GET", url);
    req.onload = function() {
      if (done) return;
      done = true;
      clearTimeout(timer);
      if (req.status >= 200 && req.status < 300 && req.responseText) ok(req.responseText);
      else fail("http " + req.status);
    };
    req.onerror = function() {
      if (done) return;
      done = true;
      clearTimeout(timer);
      fail("error");
    };
    req.send();
  } catch (err) {
    if (done) return;
    done = true;
    clearTimeout(timer);
    fail(String(err));
  }
}

function poll() {
  if (busy) return;
  busy = true;
  getText(FEED, 4000, function(text) {
    busy = false;
    try {
      publish(JSON.parse(text));
    } catch (err) {
      console.log("dps parse " + err);
    }
  }, function(why) {
    busy = false;
    console.log("dps feed " + why);
  });
}

function arm() {
  poll();
  if (pollTimer) clearInterval(pollTimer);
  pollTimer = setInterval(poll, POLL_MS);
}

Pebble.addEventListener("ready", arm);
