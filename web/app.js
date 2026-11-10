'use strict';
const initial = new URLSearchParams(location.search);
document.documentElement.dataset.theme = ['light', 'dark', 'vscode', 'ember'].includes(
  initial.get('theme')
)
  ? initial.get('theme')
  : 'light';
document.documentElement.dataset.style = initial.get('style') === 'glass' ? 'glass' : 'minimal';
const paths = {
  global: 'M3 4h18v16H3z M3 9h18 M8 9v11 M11 13h7 M11 16h5',
  windows: 'M3 4h18v16H3z M3 8h18 M8 8v12',
  presets: 'M4 4h6v6H4z M14 4h6v6h-6z M4 14h6v6H4z M14 14h6v6h-6z',
  focus: 'M8 3H3v5 M16 3h5v5 M21 16v5h-5 M8 21H3v-5 M8 8h8v8H8z',
  automation: 'm13 2-9 12h7l-1 8 10-12h-7z',
  taskbar: 'M3 5h18v14H3z M3 15h18 M9 17h.1 M12 17h.1 M15 17h.1',
  settings:
    'M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8 M9 3h6l1 3 3 1 2 5-2 5-3 1-1 3H9l-1-3-3-1-2-5 2-5 3-1z',
  sun: 'M12 2v2 M12 20v2 M2 12h2 M20 12h2 M4.93 4.93l1.42 1.42 M17.65 17.65l1.42 1.42 M4.93 19.07l1.42-1.42 M17.65 6.35l1.42-1.42 M16 12a4 4 0 1 1-8 0a4 4 0 0 1 8 0',
  moon: 'M20.9 13.1A9 9 0 0 1 10.9 3.1A9 9 0 1 0 20.9 13.1Z',
  refresh: 'M20 7v5h-5 M4 17v-5h5 M6 8a7 7 0 0 1 12-2l2 3 M4 15l2 3a7 7 0 0 0 12-2',
  pick: 'M4 3v15l4-4 4 7 3-2-4-6 6-1z',
  info: 'M12 11v6 M12 7h.01 M22 12a10 10 0 1 1-20 0 10 10 0 0 1 20 0',
  arrow: 'M5 12h14 M13 6l6 6-6 6',
  check: 'm5 12 4 4L19 6',
};
const icon = (name) =>
  `<svg viewBox="0 0 24 24" aria-hidden="true"><path d="${paths[name] || paths.windows}"/></svg>`;
const esc = (value) =>
  String(value ?? '').replace(
    /[&<>"']/g,
    (c) =>
      ({
        '&': '&amp;',
        '<': '&lt;',
        '>': '&gt;',
        '"': '&quot;',
        "'": '&#39;',
      })[c]
  );
const $ = (id) => document.getElementById(id);
const send = (action, extra = {}) => window.chrome?.webview?.postMessage({ action, ...extra });
const colors = [
  'transparent',
  '#235eda',
  '#7b41d4',
  '#c63670',
  '#c47519',
  '#188b63',
  '#485870',
  '#060c19',
];
const colorNames = ['None', 'Blue', 'Violet', 'Rose', 'Amber', 'Green', 'Slate', 'Night'];
const pages = {
  global: [
    'Global',
    'Global transparency',
    'One switch for your desktop. Separate opacity for focused and background apps.',
    '',
  ],
  windows: [
    'Windows',
    'Window transparency',
    'Fine-tune transparency without changing the way you work.',
    'WINDOW CONTROL',
  ],
  presets: [
    'Presets',
    'Presets',
    'Choose a starting point. Make it yours, one window at a time.',
    '12 COLLECTIONS',
  ],
  focus: [
    'Focus',
    'Focus mode',
    'Let your focused window take the stage, with a predictable way back.',
    'WORKSPACE',
  ],
  automation: [
    'Automation',
    'Automation',
    'Choose how Glassify handles your windows in the background.',
    'PREFERENCES',
  ],
  taskbar: [
    'Taskbar',
    'Taskbar transparency',
    'Adjust the background while icons and labels stay solid.',
    'OPTIONAL ENGINE',
  ],
  settings: [
    'Settings',
    'Settings',
    'Appearance, shortcuts, and the controls that keep you in charge.',
    'GLASSIFY',
  ],
};
let model = null,
  page = 'global',
  lastList = '',
  selectedKey = '',
  editorDirty = false,
  globalDirty = false,
  previewTimer = 0,
  toastTimer = 0;
let editor = {
  active: 100,
  inactive: 95,
  tint: 0,
  strength: 12,
  enabled: true,
  exclude: false,
};
function toast(message) {
  $('toast').textContent = message;
  $('toast').classList.add('visible');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => $('toast').classList.remove('visible'), 3200);
}
function range(name, label, description, value, min = 30, max = 100) {
  return `<div class="field"><label class="field-label" for="${name}" title="${esc(description)}">${label}</label><div class="range-line"><input type="range" id="${name}" data-range="${name}" min="${min}" max="${max}" value="${value}" aria-label="${label}" title="${esc(description)}"><label class="number-wrap"><input type="number" id="${name}-number" data-number="${name}" min="${min}" max="${max}" value="${value}" aria-label="${label} percentage"><span>%</span></label></div></div>`;
}
function toggle(key, title, description, checked) {
  return `<label class="switch-line" title="${esc(description)}"><span class="switch-copy"><strong>${title}</strong></span><span class="switch"><input type="checkbox" data-toggle="${key}" aria-label="${esc(title)}" ${checked ? 'checked' : ''}><span></span></span></label>`;
}
function hint(text) {
  return `<details class="inline-help"><summary>How it works</summary><p>${text}</p></details>`;
}
function swatches(scope, selected) {
  return `<div class="swatches" role="group" aria-label="Color tint">${colors.map((_, i) => `<button class="swatch ${i === selected ? 'active' : ''}" data-tint="${i}" data-scope="${scope}" title="${colorNames[i]}" aria-label="${colorNames[i]} tint" aria-pressed="${i === selected}"></button>`).join('')}</div>`;
}
function applySwatchColors() {
  document
    .querySelectorAll('[data-tint]')
    .forEach((b) => b.style.setProperty('--swatch', colors[Number(b.dataset.tint)]));
}
function globalPage() {
  const c = model.config,
    d = c.defaults;
  return `<section class="card global-control"><div class="card-body">${toggle('globalTransparency', 'Global transparency', 'Apply shared opacity to eligible existing and newly opened apps. Ctrl+Alt+G.', c.globalTransparency)}<div class="opacity-grid">${range('global-active', 'Focused window', 'Opacity of the app you are using.', d.activeOpacity)}${range('global-inactive', 'Background windows', 'Opacity of other windows.', d.inactiveOpacity)}</div></div><div class="card-actions"><button class="button primary" data-action="saveGlobal">Apply opacity</button><span class="help-text" id="global-save-state"></span><span class="spacer"></span><kbd>Ctrl+Alt+G</kbd><span class="badge" id="global-state">${c.globalTransparency ? 'On' : 'Off'}</span></div></section><div class="global-details"><section class="card"><div class="card-body compact-summary"><div><h2>App settings</h2><span class="muted">${model.rules.length} saved apps</span></div><button class="button" data-action="openWindows">Manage apps ${icon('arrow')}</button></div></section><section class="card"><div class="card-body">${toggle('focusMaximized', 'Minimize other windows', 'Minimize other windows when the focused app fills the screen. Ctrl+Alt+F toggles this; off restores minimized windows.', c.focusMaximized)}<div class="compact-summary"><kbd>Ctrl+Alt+F</kbd><button class="text-button" data-action="openFocus">Focus settings ${icon('arrow')}</button></div></div></section></div>${hint('Global uses shared opacity without overwriting app rules. Excluded and disabled apps are skipped. Turn it off to use app-specific settings; Pause restores all managed windows. Transparency fades window text too.')}`;
}
function windowsPage() {
  return `<div class="hint" id="global-rule-notice" ${model.config.globalTransparency ? '' : 'hidden'}><span>Global mode is using the shared opacity. Turn it off to apply individual app settings.</span><button class="text-button" data-action="openGlobal">Open Global</button></div><div class="windows-grid"><div class="stack"><section class="card"><div class="card-head"><div><h2>Open windows</h2></div><button class="icon-button" data-action="refresh" title="Refresh open windows" aria-label="Refresh open windows">${icon('refresh')}</button></div><div class="list" id="window-list"></div><div class="list-footer"><button class="button quiet" data-action="pick">${icon('pick')}Pick a window</button></div></section><section class="card"><div class="card-head"><div><h2>Saved apps</h2></div><span class="badge" id="saved-count">0</span></div><div class="list" id="rule-list"></div><div class="list-footer"><button class="button quiet" data-action="deleteRule">Remove selected settings</button></div></section>${hint('Lower opacity means more transparency. Text and content fade with the window.')}</div><section class="card" id="editor-card"></section></div>`;
}
function editorIdentity(e) {
  return `${e.key || ''}|${e.ruleId || ''}|${e.path || ''}`;
}
function renderEditor(force = false) {
  if (!model || page !== 'windows') return;
  const e = model.editor;
  const key = e.key || e.path ? editorIdentity(e) : '';
  if (!force && key === selectedKey && $('opacity-active')) {
    if (!editorDirty) {
      editor = {
        active: e.active,
        inactive: e.inactive,
        tint: e.tint,
        strength: e.strength,
        enabled: e.enabled,
        exclude: e.exclude,
      };
      for (const [id, value] of [
        ['opacity-active', e.active],
        ['opacity-inactive', e.inactive],
        ['tint-strength', e.strength],
      ]) {
        for (const suffix of ['', '-number'])
          if (document.activeElement !== $(id + suffix)) $(id + suffix).value = value;
      }
      for (const [name, value] of [
        ['enabled', e.enabled],
        ['exclude', e.exclude],
      ])
        document.querySelector('[data-toggle="editor-' + name + '"]').checked = value;
      document.querySelectorAll('[data-scope="editor"]').forEach((b) => {
        const active = Number(b.dataset.tint) === e.tint;
        b.classList.toggle('active', active);
        b.setAttribute('aria-pressed', active);
      });
      $('tint-name').textContent = colorNames[e.tint];
    }
    return;
  }
  selectedKey = key;
  editorDirty = false;
  editor = {
    active: e.active,
    inactive: e.inactive,
    tint: e.tint,
    strength: e.strength,
    enabled: e.enabled,
    exclude: e.exclude,
  };
  if (!key) {
    $('editor-card').innerHTML =
      `<div class="empty-editor"><div class="empty-art"><span></span><span></span>${icon('pick')}</div><h2>A little transparency goes a long way.</h2><button class="button" data-action="pick">${icon('pick')}Pick a window</button></div>`;
    return;
  }
  $('editor-card').innerHTML =
    `<div class="card-head"><div class="editor-title"><h2>${esc(e.name || 'Selected app')}</h2><p class="path" title="${esc(e.path)}">${esc(e.path)}</p></div><span class="badge accent">Selected app</span></div><div class="card-body"><div class="opacity-grid">${range('opacity-active', 'When focused', 'When you are working in this window.', e.active)}${range('opacity-inactive', 'In the background', 'When you switch to another window.', e.inactive)}</div><div class="divider"></div><div class="field"><div class="field-label">Color tint <span class="muted" id="tint-name">${colorNames[e.tint]}</span></div>${swatches('editor', e.tint)}${range('tint-strength', 'Tint strength', '', e.strength, 0, 40)}</div><div class="divider"></div><div class="editor-toggles">${toggle('editor-enabled', 'Transparency', 'Use these settings for this app.', e.enabled)}${toggle('editor-exclude', 'Keep opaque', 'Exclude it from automatic transparency.', e.exclude)}</div></div><div class="card-actions"><button class="button quiet" data-action="preview" ${e.key ? '' : 'disabled'}>Preview 15s</button><button class="text-button" data-action="undo">Undo</button><span class="spacer"></span><button class="button" data-action="keepWindow" ${e.key ? '' : 'disabled'} title="Keep these settings for this open window only">Keep window</button><button class="button primary" data-action="saveApp">Save for app ${icon('arrow')}</button></div>`;
  applySwatchColors();
}
function updateLists() {
  if (page !== 'windows' || !model) return;
  const fingerprint = JSON.stringify([
    model.windows,
    model.rules,
    model.editor.key,
    model.editor.ruleId,
  ]);
  if (fingerprint === lastList) return;
  lastList = fingerprint;
  $('window-list').innerHTML = model.windows.length
    ? model.windows
        .map(
          (w) =>
            `<button class="window-row ${model.editor.key === w.id ? 'active' : ''}" data-window="${esc(w.id)}"><span class="app-mark">${icon('windows')}</span><span class="row-copy"><strong title="${esc(w.title)}">${esc(w.title)}</strong><small>${esc(w.app)}</small></span></button>`
        )
        .join('')
    : `<div class="list-empty">${icon('windows')}No compatible windows found.<br>Open a normal app, then refresh or pick it.</div>`;
  $('rule-list').innerHTML = model.rules.length
    ? model.rules
        .map(
          (r) =>
            `<button class="window-row ${model.editor.ruleId === r.id ? 'active' : ''}" data-rule="${esc(r.id)}"><span class="app-mark">${esc(r.app.slice(0, 1).toUpperCase())}</span><span class="row-copy"><strong>${esc(r.app)}</strong><small>${r.exclude ? 'Keep opaque' : !r.enabled ? 'Disabled' : `${r.active}% focused · ${r.inactive}% background`}</small></span></button>`
        )
        .join('')
    : '<div class="list-empty">Your saved app settings will appear here.</div>';
  $('saved-count').textContent = model.rules.length;
}
function presetsPage() {
  return `<div class="presets-grid">${model.presets.map((p, i) => `<button class="preset ${model.config.presetId === p.id ? 'active' : ''}" data-preset="${i}"><div class="preset-preview" data-preview="${i}"><i></i><i></i></div><strong>${esc(p.name)}</strong><small>${esc(p.description)}</small></button>`).join('')}</div>${hint('Presets update enabled saved apps and the automatic default. Apps you excluded stay opaque. Individual temporary adjustments are cleared.')}`;
}
function focusPage() {
  const c = model.config;
  return `<div class="two-column"><section class="card"><div class="card-head"><h2>Focus mode</h2><kbd>Ctrl+Alt+F</kbd></div><div class="card-body">${toggle('focusMaximized', 'Minimize other windows', 'Off by default. Turning it off restores windows minimized by Glassify.', c.focusMaximized)}<div class="form-row"><label for="focus-trigger">Trigger</label><select id="focus-trigger" class="select" data-focus-setting="focusFullscreenOnly"><option value="false" ${!c.focusFullscreenOnly ? 'selected' : ''}>Maximized or full screen</option><option value="true" ${c.focusFullscreenOnly ? 'selected' : ''}>Full screen only</option></select></div><div class="form-row"><label for="focus-delay">Settle time</label><select id="focus-delay" class="select" data-focus-setting="focusDelayMs">${[
    ...new Set([150, 350, 600, 1000, c.focusDelayMs]),
  ]
    .sort((a, b) => a - b)
    .map((n) => `<option value="${n}" ${c.focusDelayMs === n ? 'selected' : ''}>${n} ms</option>`)
    .join(
      ''
    )}</select></div>${toggle('focusSameMonitor', 'Same display only', 'Keep other monitors unchanged.', c.focusSameMonitor)}${toggle('focusRestoreOnSwitch', 'Restore when switching away', 'Restore minimized windows when you switch to a normal window.', c.focusRestoreOnSwitch)}</div></section><section class="card"><div class="card-head"><h2>App exclusions</h2><span class="badge" id="focus-status">${esc(model.focusStatus)}</span></div><div class="card-body"><label class="field-label" for="focus-exclusions">Leave these apps open</label><input id="focus-exclusions" class="text-input" placeholder="music.exe, messenger.exe" value="${esc(c.focusExclusions.join(', '))}" maxlength="2048"><p class="help-text">Separate executable names with commas.</p></div><div class="card-actions"><button class="button primary" data-action="saveFocus">Save exclusions</button><button class="button" data-action="restoreFocus">Restore now</button></div></section></div>${hint('Focus is off by default. Ctrl+Alt+F toggles it permanently. Ctrl+Alt+M restores windows and suspends the current focus session. Glassify restores only windows it minimized. Other options save automatically.')}`;
}
function automationPage() {
  const c = model.config,
    d = c.defaults;
  return `<div class="two-column"><section class="card"><div class="card-head"><h2>Window behavior</h2></div><div class="card-body">${toggle('autoApply', 'Fallback transparency', 'With Global off, apply defaults to apps without a saved rule.', c.autoApply)}${toggle('mouseControl', 'Ctrl+Alt+scroll', 'Adjust the window under the pointer.', c.mouseControl)}${toggle('minimizeToTray', 'Minimize to tray', 'Keep effects running when minimized.', c.minimizeToTray)}${toggle('closeToTray', 'Close to tray', 'The close button hides Glassify; Quit stops it.', c.closeToTray)}${toggle('powerSaving', 'Respect power saving', 'Suspend window effects when Windows requests power saving.', c.powerSaving)}</div></section><section class="card"><div class="card-head"><h2>Shared defaults</h2></div><div class="card-body"><div class="opacity-grid">${range('default-active', 'Focused', 'Shared with Global.', d.activeOpacity)}${range('default-inactive', 'Background', 'Shared with Global.', d.inactiveOpacity)}</div><div class="field-label">Tint</div>${swatches('defaults', d.tint)}${range('default-strength', 'Tint strength', 'Whole-window color overlay.', d.tintStrength, 0, 40)}</div><div class="card-actions"><button class="button primary" data-action="saveDefaults">Save defaults</button></div></section></div>${hint('Global and fallback transparency share these defaults. Excluded and disabled apps are skipped. Per-app settings take priority when Global is off.')}`;
}
function taskbarPage() {
  const c = model.config,
    ready = model.taskbarInstalled,
    busy = model.taskbarSetupRunning;
  return `<section class="card taskbar-card"><div class="card-head"><h2>Taskbar background</h2><span class="badge">${ready ? 'Ready' : busy ? 'Setting up' : 'Setup needed'}</span></div><div class="card-body">${!ready ? `<div class="setup-copy compact-summary"><span>One-time engine download required.</span><button class="button primary" data-action="setupTaskbar" ${busy ? 'disabled' : ''}>${busy ? 'Setting up...' : 'Set up engine'}</button></div>` : ''}<div class="taskbar-preview"><small>Preview</small><div class="bar-bg" id="bar-background"></div><div class="bar-icons"><i></i><i></i><i></i><i></i></div></div>${toggle('taskbarEnabled', 'Taskbar transparency', 'Changes the background only. Icons and labels stay solid.', c.taskbarEnabled)}${range('taskbar-opacity', 'Background opacity', '0% clear, 100% solid. Icons stay opaque.', c.taskbarOpacity, 0, 100)}<p class="help-text" id="taskbar-result" role="status">${esc(model.taskbarStatus)}</p></div><div class="card-actions"><button class="button primary" data-action="applyTaskbar" ${!ready || busy ? 'disabled' : ''}>Apply background</button><button class="button" data-action="resetTaskbar" ${!ready || busy ? 'disabled' : ''}>Restore taskbar</button></div></section>${hint('Icons and labels stay solid. Availability depends on your Windows version. Use one taskbar tool at a time. Pause or Quit restores the taskbar.')}`;
}
function settingsPage() {
  const c = model.config;
  const themes = [
    ['light', 'Light'],
    ['vscode', 'VS Code'],
    ['dark', 'Midnight'],
    ['ember', 'Ember'],
  ];
  const shortcuts = [
    ['Pause', model.pauseShortcut],
    ['Opacity', 'Ctrl+Alt+Up / Down'],
    ['Window toggle', 'Ctrl+Alt+T'],
    ['Presets', 'Ctrl+Alt+Left / Right'],
    ['Show panel', 'Ctrl+Alt+H'],
    ['Focus mode', 'Ctrl+Alt+F'],
    ['Global', 'Ctrl+Alt+G'],
    ['Restore focus', 'Ctrl+Alt+M'],
    ['Pointer opacity', 'Ctrl+Alt+scroll'],
  ];
  return `<section class="card appearance-card"><div class="card-body"><h2>Color theme</h2><div class="theme-options" role="group" aria-label="Color theme">${themes.map(([id, title]) => `<button class="theme-option ${c.uiTheme === id ? 'active' : ''}" data-theme-choice="${id}" aria-pressed="${c.uiTheme === id}"><span class="theme-chip ${id}" aria-hidden="true"><i></i><i></i></span>${title}${id === 'light' ? '<small>Default</small>' : ''}</button>`).join('')}</div><div class="appearance-bottom"><div><h2>Interface</h2><div class="style-options" role="group" aria-label="Interface style">${['minimal', 'glass'].map((id) => `<button class="button ${c.uiStyle === id ? 'primary' : ''}" data-style-choice="${id}" aria-pressed="${c.uiStyle === id}">${id === 'minimal' ? 'Minimal' : 'Glass'}</button>`).join('')}</div></div><div><label class="field-label" for="tray-mode">Tray behavior</label><select id="tray-mode" class="select" data-tray-mode title="Instant reopen retains browser memory. Low memory releases it after hiding."><option value="true" ${c.keepInterfaceReady ? 'selected' : ''}>Instant reopen</option><option value="false" ${!c.keepInterfaceReady ? 'selected' : ''}>Low memory</option></select>${toggle('startup', 'Launch at sign-in', 'Start in the tray.', model.startup)}</div></div></div></section><div class="settings-secondary"><section class="card"><div class="card-head"><h2>Shortcuts</h2><button class="text-button" data-action="recordShortcut">Change pause shortcut</button></div><div class="card-body shortcut-grid">${shortcuts.map(([label, key]) => `<div class="shortcut"><span>${esc(label)}</span><kbd>${esc(key)}</kbd></div>`).join('')}</div></section><section class="card"><div class="card-head"><h2>Settings and recovery</h2></div><div class="card-body"><div class="recovery-grid"><button class="button" data-action="import">Import</button><button class="button" data-action="export">Export</button><button class="button" data-action="dataFolder">Data folder</button><button class="button" data-action="restore">Restore all</button><button class="button danger" data-action="quit">Quit and restore</button></div><div class="author-credit">Created by <a href="https://github.com/xaicat" data-author>xaicat ${icon('arrow')}</a></div></div></section></div>`;
}
function renderPage() {
  const focused = document.activeElement;
  const focusNav = focused?.dataset?.page,
    focusTheme = focused?.dataset?.themeChoice,
    focusStyle = focused?.dataset?.styleChoice;

  if (!model) return;
  if (page === 'global') globalDirty = false;
  if (page === 'automation') editor.defaultTint = model.config.defaults.tint;
  const info = pages[page];
  $('breadcrumb').textContent = info[0];
  $('page-title').textContent = info[1];
  $('page-description').textContent = info[2];
  $('page-badge').textContent = info[3];
  $('navigation').innerHTML = Object.entries(pages)
    .map(
      ([id, p]) =>
        `<button class="nav-item ${page === id ? 'active' : ''}" data-page="${id}" ${page === id ? 'aria-current="page"' : ''}>${icon(id)}<span>${p[0]}</span></button>`
    )
    .join('');
  $('page-content').innerHTML = {
    global: globalPage,
    windows: windowsPage,
    presets: presetsPage,
    focus: focusPage,
    automation: automationPage,
    taskbar: taskbarPage,
    settings: settingsPage,
  }[page]();
  lastList = '';
  selectedKey = '';
  updateLists();
  if (page === 'windows') renderEditor(true);
  if (page === 'automation') editor.defaultTint = model.config.defaults.tint;
  if (page === 'taskbar')
    document
      .querySelectorAll('[data-toggle=taskbarEnabled], #taskbar-opacity, #taskbar-opacity-number')
      .forEach((e) => (e.disabled = !model.taskbarInstalled || model.taskbarSetupRunning));
  applySwatchColors();
  if (page === 'presets')
    document.querySelectorAll('[data-preview]').forEach((e) => {
      const p = model.presets[Number(e.dataset.preview)];
      e.style.setProperty('--preview', p.tint ? colors[p.tint] : '#a7c5ce');
      e.style.setProperty('--opacity', p.active / 100);
    });
  if ($('bar-background'))
    $('bar-background').style.setProperty('--bar-opacity', cval('taskbarOpacity') / 100);
  if (focusNav) document.querySelector(`[data-page="${page}"]`)?.focus();
  if (focusTheme) document.querySelector(`[data-theme-choice="${focusTheme}"]`)?.focus();
  if (focusStyle) document.querySelector(`[data-style-choice="${focusStyle}"]`)?.focus();
}
function cval(key) {
  return model?.config?.[key];
}
function updateState(next) {
  const previous = model;
  model = next;
  if (previous?.previewActive && !model.previewActive) editorDirty = false;
  document.documentElement.dataset.theme = model.config.uiTheme || 'light';
  document.documentElement.dataset.style = model.config.uiStyle || 'minimal';
  const themeAction =
    model.config.uiTheme === 'light' ? 'Switch to dark theme' : 'Switch to light theme';
  $('theme-toggle').setAttribute('aria-label', themeAction);
  $('theme-toggle').title = themeAction;
  $('global-state') &&
    ($('global-state').textContent = model.config.globalTransparency ? 'On' : 'Off');
  $('global-focus-state') &&
    ($('global-focus-state').textContent = model.config.focusMaximized ? 'Focus on' : 'Focus off');
  $('theme-toggle').innerHTML = icon(model.config.uiTheme === 'light' ? 'moon' : 'sun');
  $('pause').textContent = model.paused ? 'Resume effects' : 'Pause effects';
  $('engine-state').textContent = model.paused
    ? 'Effects paused'
    : model.blocked
      ? 'Effects suspended'
      : 'Effects running';
  $('status').textContent = model.status;
  if ($('global-rule-notice')) $('global-rule-notice').hidden = !model.config.globalTransparency;
  if ($('global-state') && model.config.globalTransparency && (model.paused || model.blocked))
    $('global-state').textContent = model.paused ? 'On, effects paused' : 'On, effects suspended';
  if (
    $('global-save-state') &&
    previous &&
    JSON.stringify(previous.config.defaults) !== JSON.stringify(model.config.defaults)
  ) {
    $('global-save-state').textContent = globalDirty
      ? 'You have unapplied changes.'
      : 'Global opacity saved.';
    if (!globalDirty)
      for (const [id, value] of [
        ['global-active', model.config.defaults.activeOpacity],
        ['global-inactive', model.config.defaults.inactiveOpacity],
      ])
        for (const suffix of ['', '-number'])
          if (document.activeElement !== $(id + suffix)) $(id + suffix).value = value;
  }
  document.querySelectorAll('[data-toggle]').forEach((e) => {
    const key = e.dataset.toggle;
    if (key.startsWith('editor-')) return;
    const value = key === 'startup' ? model.startup : model.config[key];
    if (typeof value === 'boolean') e.checked = value;
  });
  if ($('tray-mode')) $('tray-mode').value = String(model.config.keepInterfaceReady);
  $('focus-status') && ($('focus-status').textContent = model.focusStatus);
  $('taskbar-result') && ($('taskbar-result').textContent = model.taskbarStatus);
  const setupButton = document.querySelector('[data-action=setupTaskbar]');
  if (setupButton) {
    setupButton.disabled = !!model.taskbarSetupRunning;
    setupButton.textContent = model.taskbarSetupRunning ? 'Setting up...' : 'Set up engine';
  }
  if (!previous) {
    if (pages[model.page]) page = model.page;
    renderPage();
    send('interfaceReady');
    return;
  }
  if (
    (previous.config.uiTheme !== model.config.uiTheme ||
      previous.config.uiStyle !== model.config.uiStyle) &&
    page === 'settings'
  ) {
    renderPage();
    return;
  }
  if (page === 'windows') {
    updateLists();
    if (
      !editorDirty ||
      selectedKey !== (model.editor.key || model.editor.path ? editorIdentity(model.editor) : '')
    )
      renderEditor(
        selectedKey !== (model.editor.key || model.editor.path ? editorIdentity(model.editor) : '')
      );
  }
  if (page === 'presets')
    document
      .querySelectorAll('[data-preset]')
      .forEach((b) =>
        b.classList.toggle(
          'active',
          model.config.presetId === model.presets[Number(b.dataset.preset)].id
        )
      );
  if (
    page === 'taskbar' &&
    (previous.taskbarInstalled !== model.taskbarInstalled ||
      previous.taskbarSetupRunning !== model.taskbarSetupRunning)
  )
    renderPage();
}
function readNumber(id, fallback) {
  const element = $(id);
  return element && Number.isFinite(element.valueAsNumber) ? element.valueAsNumber : fallback;
}
function editorPayload() {
  return {
    active: readNumber('opacity-active-number', editor.active),
    inactive: readNumber('opacity-inactive-number', editor.inactive),
    tint: editor.tint,
    strength: readNumber('tint-strength-number', editor.strength),
    enabled: editor.enabled,
    exclude: editor.exclude,
  };
}
function queuePreview() {
  editorDirty = true;
  clearTimeout(previewTimer);
  previewTimer = setTimeout(() => send('preview', { editor: editorPayload() }), 110);
}
document.addEventListener('click', (event) => {
  if (event.target.closest('a[data-author]')) {
    event.preventDefault();
    send('openAuthor');
    return;
  }
  const target = event.target.closest('button');
  if (!target || target.disabled) return;
  if (target.dataset.page) {
    clearTimeout(previewTimer);
    page = target.dataset.page;
    editorDirty = false;
    send('page', { page });
    renderPage();
    $('main').scrollTop = 0;
    return;
  }
  if (target.dataset.window) {
    editorDirty = false;
    send('selectWindow', { id: target.dataset.window });
    return;
  }
  if (target.dataset.rule) {
    editorDirty = false;
    send('selectRule', { id: target.dataset.rule });
    return;
  }
  if (target.dataset.preset !== undefined) {
    send('preset', { index: Number(target.dataset.preset) });
    return;
  }
  if (target.dataset.themeChoice) {
    send('theme', { theme: target.dataset.themeChoice });
    return;
  }
  if (target.id === 'theme-toggle') {
    send('theme', {
      theme: cval('uiTheme') === 'light' ? 'dark' : 'light',
    });
    return;
  }
  if (target.dataset.tint !== undefined) {
    const index = Number(target.dataset.tint);
    document.querySelectorAll(`[data-scope="${target.dataset.scope}"]`).forEach((b) => {
      const active = Number(b.dataset.tint) === index;
      b.classList.toggle('active', active);
      b.setAttribute('aria-pressed', active);
    });
    if (target.dataset.scope === 'editor') {
      editor.tint = index;
      $('tint-name').textContent = colorNames[index];
      queuePreview();
    } else {
      editor.defaultTint = index;
    }
    return;
  }
  if (target.dataset.styleChoice) {
    send('style', { style: target.dataset.styleChoice });
    return;
  }
  const action = target.dataset.action;
  if (!action) return;
  if (action === 'openFocus' || action === 'openWindows' || action === 'openGlobal') {
    page = action === 'openFocus' ? 'focus' : action === 'openGlobal' ? 'global' : 'windows';
    send('page', { page });
    renderPage();
    $('main').scrollTop = 0;
    return;
  }
  if (action === 'saveGlobal') {
    globalDirty = false;
    send('saveDefaults', {
      active: readNumber('global-active-number', 95),
      inactive: readNumber('global-inactive-number', 85),
      tint: cval('defaults').tint,
      strength: cval('defaults').tintStrength,
    });
    return;
  }
  clearTimeout(previewTimer);
  if (['preview', 'saveApp', 'keepWindow'].includes(action)) {
    send(action, { editor: editorPayload() });
    if (action !== 'preview') editorDirty = false;
    return;
  }
  if (action === 'saveDefaults') {
    send(action, {
      active: readNumber('default-active-number', 95),
      inactive: readNumber('default-inactive-number', 85),
      strength: readNumber('default-strength-number', 12),
      tint: editor.defaultTint ?? cval('defaults').tint,
    });

    return;
  }
  if (action === 'saveFocus') {
    send(action, {
      exclusions: $('focus-exclusions')
        .value.split(',')
        .map((s) => s.trim())
        .filter(Boolean),
    });
    return;
  }
  if (action === 'applyTaskbar') {
    send(action, { opacity: readNumber('taskbar-opacity-number', 85) });
    return;
  }
  if (action === 'setupTaskbar') {
    send(action);
    toast('Setting up taskbar transparency. This may take a moment.');
    return;
  }
  if (action === 'undo') editorDirty = false;
  send(action);
});
document.addEventListener('input', (event) => {
  const e = event.target;
  const key = e.dataset.range || e.dataset.number;
  if (!key) return;
  const fromRange = !!e.dataset.range;
  const min = Number(e.min),
    max = Number(e.max);
  let n = e.valueAsNumber;
  if (!Number.isFinite(n)) return;
  const valid = n >= min && n <= max && Number.isInteger(n);
  n = Math.max(min, Math.min(max, n));
  $(fromRange ? `${key}-number` : key).value = String(n);
  if (!fromRange && !valid) {
    editorDirty = true;
    return;
  }
  if (key.startsWith('global-')) {
    globalDirty = true;
    if ($('global-save-state')) $('global-save-state').textContent = 'Unapplied changes';
  }
  if (key.startsWith('opacity-') || key === 'tint-strength') queuePreview();
  if (key === 'taskbar-opacity' && $('bar-background'))
    $('bar-background').style.setProperty('--bar-opacity', n / 100);
});
document.addEventListener('change', (event) => {
  const e = event.target;
  if (e.hasAttribute('data-tray-mode')) {
    send('toggle', { key: 'keepInterfaceReady', value: e.value === 'true' });
    return;
  }
  if (e.dataset.number) {
    const key = e.dataset.number;
    let n = e.valueAsNumber;
    if (!Number.isFinite(n)) n = Number(e.min);
    n = Math.round(Math.max(Number(e.min), Math.min(Number(e.max), n)));
    e.value = String(n);
    $(key).value = String(n);
    if (key.startsWith('opacity-') || key === 'tint-strength') queuePreview();
    return;
  }
  if (e.dataset.toggle) {
    const key = e.dataset.toggle;
    if (key === 'editor-enabled') {
      editor.enabled = e.checked;
      queuePreview();
      return;
    }
    if (key === 'editor-exclude') {
      editor.exclude = e.checked;
      queuePreview();
      return;
    }
    send('toggle', { key, value: e.checked });
  }
  if (e.dataset.focusSetting) {
    const key = e.dataset.focusSetting;
    send('focusSetting', {
      key,
      value: key === 'focusDelayMs' ? Number(e.value) : e.value === 'true',
    });
  }
});
window.chrome?.webview?.addEventListener('message', (event) => {
  if (event.data?.type === 'state') updateState(event.data);
  if (event.data?.type === 'notice') toast(event.data.message);
});
// Exposed only inside the trusted local page for the native integration test.
window.glassify = {
  receive: updateState,
  getState: () => model,
  getPage: () => page,
};
send('ready');
