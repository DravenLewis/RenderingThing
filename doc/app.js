(function () {
  const body = document.body;
  const navToggle = document.querySelector('.nav-toggle');
  if (navToggle) {
    navToggle.addEventListener('click', function () {
      body.classList.toggle('nav-open');
    });
  }

  const topbar = document.querySelector('.topbar');
  if (topbar && !document.querySelector('.settings-toggle')) {
    const settingsToggle = document.createElement('button');
    settingsToggle.className = 'settings-toggle';
    settingsToggle.type = 'button';
    const icon = document.createElement('i');
    icon.className = 'bi bi-gear';
    icon.setAttribute('aria-hidden', 'true');
    const label = document.createElement('span');
    label.textContent = 'Settings';
    settingsToggle.appendChild(icon);
    settingsToggle.appendChild(label);
    topbar.appendChild(settingsToggle);
  }

  if (!document.getElementById('settings-panel')) {
    const panel = document.createElement('div');
    panel.id = 'settings-panel';
    panel.className = 'settings-panel';
    panel.innerHTML = `
      <h3>Theme</h3>
      <div class="settings-row" id="theme-options"></div>
      <h3>Accent Colors (Color.h)</h3>
      <div class="color-grid" id="accent-color-grid"></div>
    `;
    body.appendChild(panel);
  }

  const settingsToggle = document.querySelector('.settings-toggle');
  const settingsPanel = document.getElementById('settings-panel');
  if (settingsToggle && settingsPanel) {
    settingsToggle.addEventListener('click', function () {
      settingsPanel.classList.toggle('open');
    });
    document.addEventListener('click', function (event) {
      const target = event.target;
      if (!settingsPanel.classList.contains('open')) return;
      if (settingsPanel.contains(target) || settingsToggle.contains(target)) return;
      settingsPanel.classList.remove('open');
    });
    document.addEventListener('keydown', function (event) {
      if (event.key === 'Escape') {
        settingsPanel.classList.remove('open');
      }
    });
  }

  const page = body.getAttribute('data-page');
  if (page) {
    const activeLink = document.querySelector('[data-page-link="' + page + '"]');
    if (activeLink) activeLink.classList.add('active');
  }

  const tocRoot = document.getElementById('toc');
  if (tocRoot) {
    const headings = document.querySelectorAll('main h2, main h3');
    if (headings.length > 0) {
      const list = document.createElement('ul');
      headings.forEach(function (heading) {
        const text = heading.textContent.trim();
        if (!heading.id) {
          heading.id = text.toLowerCase().replace(/[^a-z0-9\s-]/g, '').replace(/\s+/g, '-');
        }
        const li = document.createElement('li');
        if (heading.tagName.toLowerCase() === 'h3') {
          li.style.marginLeft = '12px';
        }
        const link = document.createElement('a');
        link.href = '#' + heading.id;
        link.textContent = text;
        li.appendChild(link);
        list.appendChild(li);
      });
      tocRoot.innerHTML = '<h4>On this page</h4>';
      tocRoot.appendChild(list);
    }
  }

  function escapeHtml(str) {
    return str
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;');
  }

  function highlight(code, language) {
    let html = escapeHtml(code);
    const placeholders = [];
    const tokenPrefix = '@@RTPH_';
    const tokenSuffix = '@@';

    function stash(regex, className) {
      html = html.replace(regex, function (match) {
        const key = tokenPrefix + String(placeholders.length) + tokenSuffix;
        placeholders.push('<span class="token ' + className + '">' + match + '</span>');
        return key;
      });
    }

    const common = function () {
      stash(/\/\/.*$/gm, 'comment');
      stash(/\/\*[\s\S]*?\*\//g, 'comment');
      stash(/"(?:\\.|[^"\\])*"/g, 'string');
      stash(/'(?:\\.|[^'\\])'/g, 'string');
      stash(/\b\d+(?:\.\d+)?\b/g, 'number');
    };

    if (language === 'cpp' || language === 'c' || language === 'glsl') {
      stash(/^\s*#.*$/gm, 'preproc');
      common();
      stash(/\b(class|struct|enum|namespace|using|typedef|template|constexpr|inline|static|virtual|override|final|public|private|protected|friend|operator|const|volatile|mutable|noexcept|return|if|else|switch|case|for|while|do|break|continue|try|catch|throw|new|delete|this|sizeof|alignof|decltype|auto)\b/g, 'keyword');
      stash(/\b(void|bool|char|short|int|long|float|double|size_t|uint8_t|uint16_t|uint32_t|uint64_t|int8_t|int16_t|int32_t|int64_t|string|std|vector|shared_ptr|unique_ptr)\b/g, 'type');
      stash(/\b(true|false|nullptr)\b/g, 'literal');
      stash(/\b([A-Za-z_][A-Za-z0-9_]*)\s*(?=\()/g, 'func');
    } else if (language === 'bash' || language === 'shell' || language === 'batch') {
      stash(/#.*/g, 'comment');
      stash(/"(?:\\.|[^"\\])*"/g, 'string');
      stash(/\b(set|if|else|echo|goto|call|exit|endlocal|setlocal)\b/gi, 'keyword');
      stash(/\b\d+\b/g, 'number');
    } else {
      common();
    }

    const tokenRegex = new RegExp(tokenPrefix.replace(/[-/\\^$*+?.()|[\]{}]/g, '\\$&') + '(\\d+)' + tokenSuffix, 'g');
    html = html.replace(tokenRegex, function (_, index) {
      return placeholders[Number(index)];
    });
    // Safety: strip any leftover placeholders
    html = html.replace(new RegExp(tokenPrefix.replace(/[-/\\^$*+?.()|[\]{}]/g, '\\$&') + '\\d+' + tokenSuffix, 'g'), '');

    return html;
  }

  document.querySelectorAll('pre code').forEach(function (block) {
    const className = block.className || '';
    const match = className.match(/language-([a-z0-9]+)/i);
    const language = match ? match[1].toLowerCase() : 'text';
    const pre = block.parentElement;
    if (pre && pre.tagName.toLowerCase() === 'pre' && !pre.parentElement.classList.contains('code-block')) {
      const wrapper = document.createElement('div');
      wrapper.className = 'code-block';

      const header = document.createElement('div');
      header.className = 'code-header';

      const lang = document.createElement('div');
      lang.className = 'code-lang';
      lang.textContent = language;

      const copyBtn = document.createElement('button');
      copyBtn.className = 'copy-btn';
      copyBtn.type = 'button';
      const copyLabel = document.createElement('span');
      copyLabel.textContent = 'Copy';
      copyBtn.appendChild(copyLabel);
      const copyIcon = document.createElement('i');
      copyIcon.className = 'bi bi-clipboard';
      copyIcon.setAttribute('aria-hidden', 'true');
      copyBtn.appendChild(copyIcon);
      copyBtn.addEventListener('click', function () {
        const raw = pre.getAttribute('data-raw') || block.textContent;
        navigator.clipboard.writeText(raw).then(function () {
          copyLabel.textContent = 'Copied';
          setTimeout(function () {
            copyLabel.textContent = 'Copy';
          }, 1200);
        });
      });

      header.appendChild(lang);
      header.appendChild(copyBtn);
      wrapper.appendChild(header);

      pre.parentElement.insertBefore(wrapper, pre);
      wrapper.appendChild(pre);
    }
    const raw = block.textContent;
    if (pre) {
      pre.setAttribute('data-raw', raw);
    }
    block.innerHTML = highlight(raw, language);
  });

  const colorH = [
    { name: 'RED', hex: '#FF0000' },
    { name: 'GREEN', hex: '#00FF00' },
    { name: 'BLUE', hex: '#0000FF' },
    { name: 'MAGENTA', hex: '#FF00FF' },
    { name: 'YELLOW', hex: '#FFFF00' },
    { name: 'CYAN', hex: '#00FFFF' },
    { name: 'BLACK', hex: '#000000' },
    { name: 'WHITE', hex: '#FFFFFF' },
    { name: 'CLEAR', hex: '#00000000' },
    { name: 'GRAY', hex: '#808080' },
    { name: 'LIGHT_GRAY', hex: '#D3D3D3' },
    { name: 'DARK_GRAY', hex: '#404040' },
    { name: 'SILVER', hex: '#C0C0C0' },
    { name: 'CHARCOAL', hex: '#36454F' },
    { name: 'BEIGE', hex: '#F5F5DC' },
    { name: 'ORANGE', hex: '#FFA500' },
    { name: 'GOLD', hex: '#FFD700' },
    { name: 'CORAL', hex: '#FF7F50' },
    { name: 'CRIMSON', hex: '#DC143C' },
    { name: 'MAROON', hex: '#800000' },
    { name: 'TOMATO', hex: '#FF6347' },
    { name: 'SALMON', hex: '#FA8072' },
    { name: 'BROWN', hex: '#A52A2A' },
    { name: 'NAVY', hex: '#000080' },
    { name: 'SKY_BLUE', hex: '#87CEEB' },
    { name: 'STEEL_BLUE', hex: '#4682B4' },
    { name: 'TEAL', hex: '#008080' },
    { name: 'TURQUOISE', hex: '#40E0D0' },
    { name: 'FOREST_GREEN', hex: '#228B22' },
    { name: 'LIME', hex: '#32CD32' },
    { name: 'MINT', hex: '#98FF98' },
    { name: 'OLIVE', hex: '#808000' },
    { name: 'PURPLE', hex: '#800080' },
    { name: 'VIOLET', hex: '#EE82EE' },
    { name: 'INDIGO', hex: '#4B0082' },
    { name: 'SLATE_BLUE', hex: '#6A5ACD' },
    { name: 'LAVENDER', hex: '#E6E6FA' },
    { name: 'PINK', hex: '#FFC0CB' },
    { name: 'HOT_PINK', hex: '#FF69B4' }
  ];

  const themeOptions = [
    { id: 'light', label: 'Light' },
    { id: 'dark', label: 'Dark' },
    { id: 'system', label: 'System' }
  ];

  let currentAccent = '#0f6cbd';

  function applyTheme(theme) {
    if (theme === 'system') {
      const prefersDark = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches;
      body.setAttribute('data-theme', prefersDark ? 'dark' : 'light');
      applyAccent(currentAccent);
      return;
    }
    body.setAttribute('data-theme', theme);
    applyAccent(currentAccent);
  }

  function lighten(hex, amount) {
    const num = parseInt(hex.slice(1), 16);
    const r = Math.min(255, ((num >> 16) & 0xff) + amount);
    const g = Math.min(255, ((num >> 8) & 0xff) + amount);
    const b = Math.min(255, (num & 0xff) + amount);
    return '#' + [r, g, b].map(v => v.toString(16).padStart(2, '0')).join('');
  }

  function darken(hex, amount) {
    const num = parseInt(hex.slice(1), 16);
    const r = Math.max(0, ((num >> 16) & 0xff) - amount);
    const g = Math.max(0, ((num >> 8) & 0xff) - amount);
    const b = Math.max(0, (num & 0xff) - amount);
    return '#' + [r, g, b].map(v => v.toString(16).padStart(2, '0')).join('');
  }

  function applyAccent(hex) {
    currentAccent = hex;
    body.style.setProperty('--brand', hex);
    body.style.setProperty('--brand-strong', darken(hex, 18));
    const isDark = body.getAttribute('data-theme') === 'dark';
    if (isDark) {
      body.style.setProperty('--brand-soft', 'rgba(15, 108, 189, 0.2)');
    } else {
      body.style.setProperty('--brand-soft', lighten(hex, 210));
    }
    body.style.setProperty('--accent', hex);
  }

  function initThemeUI() {
    const themeRoot = document.getElementById('theme-options');
    const colorRoot = document.getElementById('accent-color-grid');
    if (!themeRoot || !colorRoot) return;

    const storedTheme = sessionStorage.getItem('rt-docs-theme') || localStorage.getItem('rt-docs-theme') || 'system';
    const storedAccent = sessionStorage.getItem('rt-docs-accent') || localStorage.getItem('rt-docs-accent') || '#0f6cbd';

    applyTheme(storedTheme);
    applyAccent(storedAccent);

    themeRoot.innerHTML = '';
    themeOptions.forEach(function (opt) {
      const btn = document.createElement('button');
      btn.className = 'theme-option' + (opt.id === storedTheme ? ' active' : '');
      btn.type = 'button';
      btn.textContent = opt.label;
      btn.addEventListener('click', function () {
        sessionStorage.setItem('rt-docs-theme', opt.id);
        applyTheme(opt.id);
        themeRoot.querySelectorAll('.theme-option').forEach(function (b) { b.classList.remove('active'); });
        btn.classList.add('active');
      });
      themeRoot.appendChild(btn);
    });

    colorRoot.innerHTML = '';
    colorH.forEach(function (c) {
      const btn = document.createElement('button');
      btn.className = 'color-swatch' + (c.hex.toLowerCase() === storedAccent.toLowerCase() ? ' active' : '');
      btn.type = 'button';
      btn.setAttribute('aria-label', c.name + ' ' + c.hex);
      btn.title = c.name + ' ' + c.hex;

      const chip = document.createElement('div');
      chip.className = 'color-chip';
      chip.style.background = c.hex;

      btn.appendChild(chip);

      btn.addEventListener('click', function () {
        sessionStorage.setItem('rt-docs-accent', c.hex);
        applyAccent(c.hex);
        colorRoot.querySelectorAll('.color-swatch').forEach(function (b) { b.classList.remove('active'); });
        btn.classList.add('active');
      });

      colorRoot.appendChild(btn);
    });
  }

  initThemeUI();
})();
