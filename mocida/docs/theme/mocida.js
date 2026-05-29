/*
 * Mocida docs — interactive theme switcher.
 *
 * Modes:
 *   auto   (no localStorage entry) — follow prefers-color-scheme
 *   light  — force light, regardless of OS
 *   dark   — force dark, regardless of OS
 *
 * The synchronous bootstrap in header.html applies the persisted choice
 * before paint to avoid a theme flash. This script wires the button.
 */
/* Ensure the sidebar starts open. Doxygen persists the last drag width
 * in two cookies (doxygen_width / doxygen_pagenav_width). If either is
 * 0 or below a usable threshold (user collapsed it once, or it never
 * received a value), promote it to a sane default BEFORE navtree.js
 * reads them. Runs as early as possible to avoid a width-0 flash. */
(function () {
    var DEFAULT_WIDTH = 290;
    var MIN_WIDTH = 200;
    function bumpCookie(name) {
        var v = parseInt(document.cookie.split(';').map(function (s) {
            return s.trim();
        }).filter(function (s) {
            return s.indexOf(name + '=') === 0;
        }).map(function (s) {
            return s.substring(name.length + 1);
        })[0] || '0', 10);
        if (!v || v < MIN_WIDTH) {
            document.cookie = name + '=' + DEFAULT_WIDTH + ';path=/;max-age=31536000;SameSite=Lax';
        }
    }
    bumpCookie('doxygen_width');
    bumpCookie('doxygen_pagenav_width');
})();

(function () {
    var STORAGE_KEY = 'mocida-theme';

    var ICONS = {
        auto:  '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><circle cx="12" cy="12" r="9"/><path d="M12 3v18"/><path d="M12 12a9 9 0 0 1 0-9"/><path d="M12 12a9 9 0 0 0 0 9" fill="currentColor" opacity=".25"/></svg>',
        light: '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2M20 12h2M4.93 19.07l1.41-1.41M17.66 6.34l1.41-1.41"/></svg>',
        dark:  '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/></svg>'
    };

    var LABELS = {
        auto:  'Theme: auto (system)',
        light: 'Theme: light',
        dark:  'Theme: dark'
    };

    function currentMode() {
        try {
            var v = localStorage.getItem(STORAGE_KEY);
            if (v === 'light' || v === 'dark') return v;
        } catch (e) { /* localStorage blocked */ }
        return 'auto';
    }

    function applyMode(mode) {
        var html = document.documentElement;
        if (mode === 'light' || mode === 'dark') {
            html.setAttribute('data-theme', mode);
        } else {
            html.removeAttribute('data-theme');
        }
        try {
            if (mode === 'auto') localStorage.removeItem(STORAGE_KEY);
            else localStorage.setItem(STORAGE_KEY, mode);
        } catch (e) { /* ignore */ }
    }

    function nextMode(mode) {
        if (mode === 'auto')  return 'light';
        if (mode === 'light') return 'dark';
        return 'auto';
    }

    function renderButton(btn) {
        var m = currentMode();
        btn.innerHTML = ICONS[m];
        btn.setAttribute('aria-label', LABELS[m]);
        btn.title = LABELS[m] + ' (click to cycle)';
        btn.dataset.mode = m;
    }

    function buildButton() {
        var btn = document.createElement('button');
        btn.id = 'mocida-theme-toggle';
        btn.type = 'button';
        renderButton(btn);
        btn.addEventListener('click', function () {
            var next = nextMode(currentMode());
            applyMode(next);
            renderButton(btn);
        });
        return btn;
    }

    function mount() {
        var btn = buildButton();
        var slot = document.getElementById('mocida-theme-slot');
        if (slot) {
            slot.appendChild(btn);
            return;
        }
        // Fallbacks for pages where header.html's slot isn't present
        // (Doxygen sometimes emits abbreviated headers for sub-pages).
        var titlearea = document.getElementById('titlearea');
        if (titlearea) {
            btn.classList.add('mocida-theme-toggle-floating');
            titlearea.appendChild(btn);
            return;
        }
        document.body.appendChild(
            Object.assign(btn, { className: 'mocida-theme-toggle-floating' })
        );
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', mount);
    } else {
        mount();
    }
})();
