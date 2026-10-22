// Loads .gs files the way Apps Script does: one shared scope, files in the given order.
// Every top-level `function name` and `var name` (column 0) is returned in an object, so tests call them directly.
// `globals` are passed in as parameters, so a .gs file sees mocks (MailApp, Date, ...) only when a test gives them;
// Lib.gs is loaded with none, which proves it needs no Apps Script service.
'use strict';
const fs = require('fs');
const path = require('path');

const DIR = path.join(__dirname, '..');

function loadGs(files, globals) {
  globals = globals || {};
  const src = files.map((f) => fs.readFileSync(path.join(DIR, f), 'utf8') + '\n//# sourceURL=' + f).join('\n;\n');
  const names = new Set();
  for (const m of src.matchAll(/^(?:function\s+([A-Za-z_$][\w$]*)|var\s+([A-Za-z_$][\w$]*))/gm)) names.add(m[1] || m[2]);
  const body = '"use strict";\n' + src + '\nreturn {' + [...names].map((n) => `${n}: ${n}`).join(', ') + '};';
  const keys = Object.keys(globals);
  // eslint-disable-next-line no-new-func
  return new Function(...keys, body)(...keys.map((k) => globals[k]));
}

module.exports = { loadGs, DIR };
