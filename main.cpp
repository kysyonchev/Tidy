// Tidy - visual Homebrew / app cleaner for macOS (Qt 6 Widgets)
#include <QtWidgets>
#include <algorithm>

struct Item {
    enum Kind { Formula, Cask, App, Junk, Large } kind = Formula;
    QString name, desc, version, path, bundleId, homepage;
    QStringList deps, usedBy;
    bool onRequest = true;
};

static QString g_brew, g_prefix;
static const int R_ACT = Qt::UserRole, R_KB = Qt::UserRole + 1;

static QString run(const QString &prog, const QStringList &args, int ms = 300000) {
    QProcess p;
    p.start(prog, args);
    if (!p.waitForFinished(ms)) { p.kill(); return {}; }
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}
static bool runOk(const QString &prog, const QStringList &args, QString *out) {
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(prog, args);
    bool ok = p.waitForFinished(600000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    if (out) *out = QString::fromUtf8(p.readAll()).trimmed();
    return ok;
}
static QString human(qint64 kb) { return QLocale().formattedDataSize(kb * 1024, 1); }

// ---------------------------------------------------------------- dependency graph
class GraphView : public QGraphicsView {
    Q_OBJECT
    QGraphicsScene sc;
public:
    explicit GraphView(QWidget *p = nullptr) : QGraphicsView(p) {
        setScene(&sc);
        setRenderHint(QPainter::Antialiasing);
        setFrameShape(QFrame::NoFrame);
        setStyleSheet("background:transparent;");
    }
    void setNode(const QString &name, const QStringList &deps, const QStringList &usedBy) {
        sc.clear();
        if (name.isEmpty()) return;
        const QColor fg = palette().color(QPalette::Text);
        auto node = [&](const QString &label, QPointF c, QColor fill, QColor txt, bool link) {
            QPainterPath pp;
            pp.addRoundedRect(QRectF(-75, -15, 150, 30), 8, 8);
            auto *r = sc.addPath(pp, Qt::NoPen, QBrush(fill));
            r->setPos(c);
            auto *t = sc.addText("");
            t->setParentItem(r);
            t->setDefaultTextColor(txt);
            t->setPlainText(QFontMetrics(t->font()).elidedText(label, Qt::ElideRight, 136));
            t->setPos(-t->boundingRect().width() / 2, -t->boundingRect().height() / 2);
            if (link) { r->setData(0, label); r->setCursor(Qt::PointingHandCursor); }
        };
        int maxRows = std::max({1, int(std::min<qsizetype>(deps.size(), 13)), int(std::min<qsizetype>(usedBy.size(), 13))});
        qreal y0 = -(maxRows * 19.0) - 30;
        auto column = [&](const QStringList &names, qreal x, int dir, QColor fill, const QString &head) {
            if (names.isEmpty()) return;
            QStringList show = names.mid(0, 12);
            bool more = names.size() > 12;
            if (more) show << QString("+%1 more").arg(names.size() - 12);
            auto *h = sc.addText(head);
            h->setDefaultTextColor(Qt::gray);
            h->setPos(x - h->boundingRect().width() / 2, y0);
            for (int i = 0; i < show.size(); ++i) {
                qreal y = (i - (show.size() - 1) / 2.0) * 38;
                auto *ln = sc.addLine(QLineF(x - dir * 75, y, dir * 75, 0), QPen(QColor(128, 128, 128, 140), 1.5));
                ln->setZValue(-1);
                node(show[i], {x, y}, fill, fg, !(more && i == show.size() - 1));
            }
        };
        column(deps, -260, -1, QColor(52, 199, 89, 80), "DEPENDS ON");
        column(usedBy, 260, 1, QColor(255, 159, 10, 90), "NEEDED BY");
        node(name, {0, 0}, QColor("#0a84ff"), Qt::white, false);
        if (deps.isEmpty() && usedBy.isEmpty()) {
            auto *t = sc.addText("No tracked connections");
            t->setDefaultTextColor(Qt::gray);
            t->setPos(-t->boundingRect().width() / 2, 25);
        }
        setSceneRect(QRectF(-360, y0, 720, -2 * y0));
        fitInView(sceneRect(), Qt::KeepAspectRatio);
    }
signals:
    void nodeClicked(const QString &name);
protected:
    void mousePressEvent(QMouseEvent *e) override {
        QGraphicsItem *i = itemAt(e->pos());
        while (i && i->data(0).isNull()) i = i->parentItem();
        if (i) emit nodeClicked(i->data(0).toString());
        QGraphicsView::mousePressEvent(e);
    }
    void resizeEvent(QResizeEvent *e) override {
        QGraphicsView::resizeEvent(e);
        if (!sc.items().isEmpty()) fitInView(sceneRect(), Qt::KeepAspectRatio);
    }
};

// ---------------------------------------------------------------- main window
class MainWindow : public QMainWindow {
    Q_OBJECT
    QVector<Item> items;
    QHash<QString, qint64> sizeCache;
    QListWidget *side, *list, *targets;
    QLineEdit *search;
    QLabel *title, *desc, *meta, *freeLbl;
    GraphView *graph;
    QPushButton *del, *reveal, *home;
    QComboBox *filter;
    QLabel *diskLbl;
    QString revealPath;
    int cur = -1;

    qint64 sizeOf(const QString &p) {
        auto it = sizeCache.find(p);
        if (it != sizeCache.end()) return *it;
        qint64 kb = run("/usr/bin/du", {"-sk", p}, 60000).split(QRegularExpression("\\s+")).value(0).toLongLong();
        sizeCache[p] = kb;
        return kb;
    }
    int find(const QString &n, Item::Kind k = Item::Formula) const {
        for (int i = 0; i < items.size(); ++i)
            if (items[i].kind == k && items[i].name == n) return i;
        return -1;
    }
    // files an app leaves behind; bool = ticked by default (exact bundle-id matches only)
    QList<QPair<QString, bool>> leftovers(const Item &it) const {
        QList<QPair<QString, bool>> out;
        const QString L = QDir::homePath() + "/Library/", id = it.bundleId;
        QStringList byId, byName;
        if (!id.isEmpty())
            byId << L + "Application Support/" + id << L + "Caches/" + id << L + "Preferences/" + id + ".plist"
                 << L + "Containers/" + id << L + "Saved Application State/" + id + ".savedState"
                 << L + "HTTPStorages/" + id << L + "WebKit/" + id << L + "Logs/" + id
                 << L + "Application Scripts/" + id << L + "Cookies/" + id + ".binarycookies";
        const QString n = QFileInfo(it.path).completeBaseName();
        byName << L + "Application Support/" + n << L + "Caches/" + n << L + "Logs/" + n;
        for (auto &p : byId) if (QFileInfo::exists(p)) out.append({p, true});
        for (auto &p : byName) if (QFileInfo::exists(p)) out.append({p, false});
        return out;
    }

public:
    MainWindow() {
        setWindowTitle("Tidy");
        resize(1180, 740);
        side = new QListWidget;
        side->addItems({"", "", "", "", ""});
        side->setObjectName("side");
        side->setFixedWidth(200);
        side->setCurrentRow(0);
        search = new QLineEdit;
        search->setPlaceholderText("Search");
        search->setClearButtonEnabled(true);
        list = new QListWidget;
        auto *mid = new QWidget;
        auto *ml = new QVBoxLayout(mid);
        filter = new QComboBox;
        filter->addItems({"All", "Installed by me", "Dependencies", "Orphans (safe to remove)"});
        connect(filter, &QComboBox::currentIndexChanged, this, [this] { populate(); });
        ml->addWidget(search);
        ml->addWidget(filter);
        ml->addWidget(list);
        mid->setMinimumWidth(250);
        mid->setMaximumWidth(340);

        title = new QLabel;
        title->setStyleSheet("font-size:26px;font-weight:700;");
        desc = new QLabel;
        desc->setWordWrap(true);
        meta = new QLabel;
        meta->setStyleSheet("color:gray;");
        graph = new GraphView;
        graph->setMinimumHeight(230);
        targets = new QListWidget;
        targets->setMaximumHeight(170);
        freeLbl = new QLabel;
        del = new QPushButton("Remove Selected");
        del->setObjectName("danger");

        auto card = [](QWidget *w) {
            auto *f = new QFrame;
            f->setObjectName("card");
            auto *l = new QVBoxLayout(f);
            l->setContentsMargins(8, 8, 8, 8);
            l->addWidget(w);
            return f;
        };
        auto *right = new QWidget;
        auto *rl = new QVBoxLayout(right);
        rl->setContentsMargins(22, 18, 22, 18);
        rl->addWidget(title);
        rl->addWidget(desc);
        rl->addWidget(meta);
        auto *acts = new QHBoxLayout;
        reveal = new QPushButton("Reveal in Finder");
        home = new QPushButton("Homepage");
        acts->addWidget(reveal);
        acts->addWidget(home);
        acts->addStretch();
        rl->addLayout(acts);
        connect(reveal, &QPushButton::clicked, this, [this] { QProcess::startDetached("/usr/bin/open", {"-R", revealPath}); });
        connect(home, &QPushButton::clicked, this, [this] { if (cur >= 0) QDesktopServices::openUrl(QUrl(items[cur].homepage)); });
        rl->addWidget(new QLabel("<b>Connections</b>  <span style='color:gray'>(click a node to jump to it)</span>"));
        rl->addWidget(card(graph), 1);
        rl->addWidget(new QLabel("<b>What will be removed</b>"));
        rl->addWidget(card(targets));
        auto *bar = new QHBoxLayout;
        bar->addWidget(freeLbl, 1);
        bar->addWidget(del);
        rl->addLayout(bar);

        auto *sp = new QSplitter;
        sp->addWidget(side);
        sp->addWidget(mid);
        sp->addWidget(right);
        sp->setStretchFactor(2, 1);
        sp->setChildrenCollapsible(false);
        setCentralWidget(sp);

        setUnifiedTitleAndToolBarOnMac(true);
        diskLbl = new QLabel;
        statusBar()->addPermanentWidget(diskLbl);
        auto *tb = addToolBar("Tools");
        tb->setMovable(false);
        auto mk = [&](const QString &t, auto fn, const QKeySequence &ks = QKeySequence(), QAction::MenuRole role = QAction::TextHeuristicRole) {
            auto *a = new QAction(t, this);
            a->setShortcut(ks);
            a->setMenuRole(role);
            connect(a, &QAction::triggered, this, fn);
            return a;
        };
        auto *aRefresh = mk("Refresh", [this] { loadAll(); }, QKeySequence("Ctrl+R"));
        auto *aUpgrade = mk("Update && Upgrade Homebrew", [this] {
            if (QMessageBox::question(this, "Tidy", "Run 'brew update' and 'brew upgrade'? This can take several minutes and the window will pause.") != QMessageBox::Yes) return;
            runBulk({{"update"}, {"upgrade"}});
        });
        auto *aOrph = mk("Remove Orphaned Dependencies", [this] {
            if (QMessageBox::question(this, "Tidy", "Run 'brew autoremove'? This uninstalls dependencies nothing needs anymore.") != QMessageBox::Yes) return;
            runBulk({{"autoremove"}});
        });
        auto *aCache = mk("Clear Homebrew Cache", [this] { runBulk({{"cleanup", "--prune=all", "-s"}}); });
        auto *aLarge = mk("Find Large Files...", [this] { scanLarge(); });
        auto *aExport = mk("Export Brewfile...", [this] { exportBrewfile(); });
        auto *aAbout = mk("About Tidy", [this] { showAbout(); }, QKeySequence(), QAction::AboutRole);
        auto *aQt = mk("About Qt", [] { QApplication::aboutQt(); }, QKeySequence(), QAction::AboutQtRole);
        auto *aQuit = mk("Quit Tidy", [] { QApplication::quit(); }, QKeySequence::Quit, QAction::QuitRole);
        menuBar()->addMenu("File")->addActions({aRefresh, aExport, aQuit});
        menuBar()->addMenu("Tools")->addActions({aUpgrade, aOrph, aCache, aLarge});
        menuBar()->addMenu("Help")->addActions({aAbout, aQt});
        tb->addActions({aRefresh, aUpgrade, aOrph, aCache, aLarge, aExport});

        connect(side, &QListWidget::currentRowChanged, this, [this] { populate(); });
        connect(search, &QLineEdit::textChanged, this, [this] { populate(); });
        connect(list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *i) {
            if (i) showItem(i->data(R_ACT).toInt());
        });
        connect(graph, &GraphView::nodeClicked, this, &MainWindow::selectByName);
        connect(targets, &QListWidget::itemChanged, this, [this] { updateFree(); });
        connect(del, &QPushButton::clicked, this, &MainWindow::removeSelected);
    }

    void loadAll() {
        setCursor(Qt::WaitCursor);
        statusBar()->showMessage("Reading Homebrew and Applications…");
        QApplication::processEvents();
        QVector<Item> keepLarge;
        for (auto &i : items) if (i.kind == Item::Large && QFileInfo::exists(i.path)) keepLarge << i;
        items.clear();
        sizeCache.clear();
        cur = -1;
        if (!g_brew.isEmpty()) {
            QJsonObject doc = QJsonDocument::fromJson(run(g_brew, {"info", "--json=v2", "--installed"}).toUtf8()).object();
            for (auto v : doc["formulae"].toArray()) {
                QJsonObject o = v.toObject();
                QJsonObject inst = o["installed"].toArray().first().toObject();
                Item it;
                it.kind = Item::Formula;
                it.name = o["name"].toString();
                it.desc = o["desc"].toString();
                it.version = inst["version"].toString();
                it.homepage = o["homepage"].toString();
                it.onRequest = inst["installed_on_request"].toBool();
                for (auto d : o["dependencies"].toArray()) it.deps << d.toString();
                items << it;
            }
            for (auto v : doc["casks"].toArray()) {
                QJsonObject o = v.toObject();
                Item it;
                it.kind = Item::Cask;
                it.name = o["token"].toString();
                it.desc = o["desc"].toString();
                it.version = o["installed"].toString();
                it.homepage = o["homepage"].toString();
                for (auto a : o["artifacts"].toArray()) {
                    QJsonArray ar = a.toObject()["app"].toArray();
                    if (!ar.isEmpty() && ar[0].isString()) { it.path = "/Applications/" + ar[0].toString(); break; }
                }
                items << it;
            }
        }
        for (const QString &dir : {QString("/Applications"), QDir::homePath() + "/Applications"})
            for (const QFileInfo &f : QDir(dir).entryInfoList({"*.app"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
                Item it;
                it.kind = Item::App;
                it.name = f.completeBaseName();
                it.path = f.absoluteFilePath();
                it.desc = "Application in " + dir;
                items << it;
            }
        std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        });
        const QString H = QDir::homePath() + "/";
        struct J { const char *n, *p, *d; };
        for (J j : {J{"User Caches", "Library/Caches", "Temporary data apps rebuild automatically (includes Homebrew and pip caches)."},
                    J{"Logs", "Library/Logs", "Old app and crash logs."},
                    J{"Trash", ".Trash", "Files you already deleted."},
                    J{"Xcode DerivedData", "Library/Developer/Xcode/DerivedData", "Build output. Xcode regenerates it."},
                    J{"Xcode Device Support", "Library/Developer/Xcode/iOS DeviceSupport", "Debug symbols for iPhones you plugged in; re-downloaded when needed."},
                    J{"npm cache", ".npm/_cacache", "Downloaded npm packages."},
                    J{"Cargo registry cache", ".cargo/registry/cache", "Downloaded Rust crates."},
                    J{"Gradle caches", ".gradle/caches", "Java/Android build caches."},
                    J{"iPhone and iPad backups", "Library/Application Support/MobileSync/Backup", "Local device backups. Unticked by default - only clear if you no longer need them!"}}) {
            if (!QFileInfo::exists(H + j.p)) continue;
            Item it;
            it.kind = Item::Junk;
            it.name = j.n;
            it.path = H + j.p;
            it.desc = j.d;
            items << it;
        }
        items += keepLarge;
        QSet<QString> installed;
        for (auto &it : items) if (it.kind == Item::Formula) installed.insert(it.name);
        QHash<QString, QStringList> rev;
        for (auto &it : items) {
            if (it.kind != Item::Formula) continue;
            QStringList keep;
            for (auto &d : it.deps) if (installed.contains(d)) { keep << d; rev[d] << it.name; }
            it.deps = keep;
        }
        for (auto &it : items) if (it.kind == Item::Formula) it.usedBy = rev.value(it.name);
        populate();
        unsetCursor();
        updateDisk();
        statusBar()->showMessage(g_brew.isEmpty() ? "Homebrew not found - showing applications only" : "Ready", 5000);
    }

    void populate() {
        auto kind = Item::Kind(std::max(0, side->currentRow()));
        static const char *names[] = {"🍺  Formulae", "📦  Casks", "🧩  Applications", "🧹  System Junk", "🐘  Large Files"};
        int cnt[5] = {};
        for (const Item &i : items) cnt[i.kind]++;
        for (int i = 0; i < 5; ++i) side->item(i)->setText(QString("%1    %2").arg(QString::fromUtf8(names[i])).arg(cnt[i]));
        filter->setVisible(kind == Item::Formula);
        const QString q = search->text().trimmed();
        QListWidgetItem *sel = nullptr;
        {
            QSignalBlocker b(list);
            list->clear();
            for (int i = 0; i < items.size(); ++i) {
                const Item &it = items[i];
                if (it.kind != kind || (!q.isEmpty() && !it.name.contains(q, Qt::CaseInsensitive))) continue;
                int f = filter->currentIndex();
                if (kind == Item::Formula && ((f == 1 && !it.onRequest) || (f == 2 && it.onRequest) || (f == 3 && (it.onRequest || !it.usedBy.isEmpty())))) continue;
                bool dep = it.kind == Item::Formula && !it.onRequest;
                auto *li = new QListWidgetItem(it.name + (dep ? "   · dependency" : ""));
                li->setData(R_ACT, i);
                li->setToolTip(it.desc);
                if (dep) li->setForeground(Qt::gray);
                list->addItem(li);
                if (i == cur) sel = li;
            }
        }
        if (!sel && list->count()) sel = list->item(0);
        if (sel) list->setCurrentItem(sel);
        else {
            cur = -1;
            title->clear(); desc->clear(); meta->clear();
            reveal->setEnabled(false); home->setEnabled(false);
            graph->setNode("", {}, {});
            targets->clear();
            updateFree();
        }
    }

    void selectByName(const QString &n) {
        int idx = find(n);
        if (idx < 0) return;
        cur = idx;
        search->clear();
        side->setCurrentRow(0);
        populate();
    }

    void showItem(int idx) {
        cur = idx;
        Item &it = items[idx];
        if ((it.kind == Item::Cask || it.kind == Item::App) && it.bundleId.isEmpty() && QFileInfo::exists(it.path)) {
            auto clean = [](QString s) { return s == "(null)" ? QString() : s; };
            it.bundleId = clean(run("/usr/bin/mdls", {"-raw", "-name", "kMDItemCFBundleIdentifier", it.path}, 20000));
            if (it.kind == Item::App) it.version = clean(run("/usr/bin/mdls", {"-raw", "-name", "kMDItemVersion", it.path}, 20000));
        }
        const QString root = it.kind == Item::Formula ? g_prefix + "/Cellar/" + it.name : it.path;
        qint64 kb = QFileInfo::exists(root) ? sizeOf(root) : 0;
        title->setText(it.name);
        desc->setText(it.desc.isEmpty() ? "No description available." : it.desc);
        QString kindTxt = it.kind == Item::Formula ? (it.onRequest ? "Installed by you" : "Installed as a dependency")
                        : it.kind == Item::Cask ? "Homebrew cask" : it.kind == Item::Junk ? "Safe cleanup location" : it.kind == Item::Large ? "Large file" : "Application";
        meta->setText(QString("%1  ·  %2  ·  %3").arg(it.version.isEmpty() ? "-" : "v" + it.version, kindTxt, human(kb)));
        graph->setNode(it.name, it.deps, it.usedBy);
        revealPath = QFileInfo::exists(root) ? root : QString();
        reveal->setEnabled(!revealPath.isEmpty());
        home->setEnabled(!it.homepage.isEmpty());

        QSignalBlocker b(targets);
        targets->clear();
        auto add = [&](const QString &label, const QString &act, qint64 k, bool checked = true) {
            auto *t = new QListWidgetItem(label + (k > 0 ? "  -  " + human(k) : ""));
            t->setData(R_ACT, act);
            t->setData(R_KB, qlonglong(k));
            t->setFlags(t->flags() | Qt::ItemIsUserCheckable);
            t->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
            targets->addItem(t);
        };
        if (it.kind == Item::Formula) {
            add("Uninstall " + it.name + " (Homebrew)", "formula:" + it.name, kb);
            const QString dl = QDir::homePath() + "/Library/Caches/Homebrew/downloads";
            for (const QString &f : QDir(dl).entryList({"*--" + it.name + "--*"}, QDir::Files))
                add("Cached download: " + f, "path:" + dl + "/" + f, sizeOf(dl + "/" + f));
            for (const QString &d : it.deps) {
                int di = find(d);
                if (di >= 0 && !items[di].onRequest && items[di].usedBy == QStringList{it.name})
                    add("Dependency nothing else needs: " + d, "formula:" + d, sizeOf(g_prefix + "/Cellar/" + d));
            }
        } else if (it.kind == Item::Junk) {
            add("Empty " + it.name + " (contents deleted permanently)", "clear:" + it.path, kb, !it.name.contains("backups"));
        } else if (it.kind == Item::Large) {
            add("Move to Trash: " + it.path.mid(QDir::homePath().size() + 1), "path:" + it.path, kb);
        } else {
            if (it.kind == Item::Cask) add("Uninstall " + it.name + " and run its cleanup (brew --zap)", "cask:" + it.name, kb);
            else add("Move " + it.name + ".app to the Trash", "path:" + it.path, kb);
            if (QFileInfo::exists(it.path))
                for (auto &lp : leftovers(it))
                    add((lp.second ? "Leftover: " : "Possible leftover (by name): ") + lp.first.mid(QDir::homePath().size() + 1),
                        "path:" + lp.first, sizeOf(lp.first), lp.second);
        }
        b.unblock();
        updateFree();
    }

    void updateFree() {
        qint64 kb = 0;
        int n = 0;
        for (int i = 0; i < targets->count(); ++i)
            if (targets->item(i)->checkState() == Qt::Checked) { kb += targets->item(i)->data(R_KB).toLongLong(); ++n; }
        bool blocked = cur >= 0 && items[cur].kind == Item::Formula && !items[cur].usedBy.isEmpty();
        if (blocked) freeLbl->setText("Needed by " + items[cur].usedBy.join(", ") + " - remove those first.");
        else freeLbl->setText(n ? "Frees about " + human(kb) : "Nothing selected");
        del->setEnabled(!blocked && n > 0);
    }

    void removeSelected() {
        if (cur < 0) return;
        QStringList acts, labels;
        for (int i = 0; i < targets->count(); ++i)
            if (targets->item(i)->checkState() == Qt::Checked) {
                acts << targets->item(i)->data(R_ACT).toString();
                labels << targets->item(i)->text();
            }
        if (acts.isEmpty()) return;
        QMessageBox mb(this);
        mb.setIcon(QMessageBox::Warning);
        mb.setText("Remove " + items[cur].name + "?");
        mb.setInformativeText("This will:\n• " + labels.join("\n• ") +
                              "\n\nFiles go to the Trash. Homebrew packages and emptied cache folders are deleted permanently.");
        mb.setStandardButtons(QMessageBox::Cancel | QMessageBox::Yes);
        mb.button(QMessageBox::Yes)->setText("Remove");
        if (mb.exec() != QMessageBox::Yes) return;

        setCursor(Qt::WaitCursor);
        QStringList problems;
        for (const QString &a : acts) {
            const QString kind = a.section(':', 0, 0), val = a.section(':', 1);
            statusBar()->showMessage("Removing " + val + "…");
            QApplication::processEvents();
            QString out;
            if (kind == "formula") { if (!runOk(g_brew, {"uninstall", val}, &out)) problems << out; }
            else if (kind == "cask") { if (!runOk(g_brew, {"uninstall", "--cask", "--zap", val}, &out)) problems << out; }
            else if (kind == "clear") {
                QDir d(val);
                for (const QString &e : d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
                    QFileInfo fi(d.filePath(e));
                    if (fi.isDir() && !fi.isSymLink()) QDir(fi.filePath()).removeRecursively();
                    else QFile::remove(fi.filePath());
                }
            }
            else if (!QFile::moveToTrash(val)) problems << "Couldn't move to Trash: " + val;
        }
        unsetCursor();
        loadAll();
        if (!problems.isEmpty()) QMessageBox::warning(this, "Tidy", "Some steps failed:\n\n" + problems.join("\n\n"));
        else statusBar()->showMessage("Done", 5000);
    }

    void updateDisk() {
        QStorageInfo s = QStorageInfo::root();
        diskLbl->setText(QString("Free space: %1 of %2").arg(QLocale().formattedDataSize(s.bytesAvailable()), QLocale().formattedDataSize(s.bytesTotal())));
    }

    void scanLarge() {
        setCursor(Qt::WaitCursor);
        statusBar()->showMessage("Scanning your home folder for files over 500 MB (can take a minute)...");
        QApplication::processEvents();
        QString out = run("/usr/bin/find", {QDir::homePath(), "-type", "f", "-size", "+500M", "-not", "-path", "*/Library/*", "-not", "-path", "*/.Trash/*"}, 600000);
        QStringList paths = out.split('\n', Qt::SkipEmptyParts);
        std::sort(paths.begin(), paths.end(), [](const QString &a, const QString &b) { return QFileInfo(a).size() > QFileInfo(b).size(); });
        items.erase(std::remove_if(items.begin(), items.end(), [](const Item &i) { return i.kind == Item::Large; }), items.end());
        for (const QString &p : paths) {
            Item it;
            it.kind = Item::Large;
            it.name = QFileInfo(p).fileName();
            it.path = p;
            it.desc = p;
            items << it;
        }
        cur = -1;
        side->setCurrentRow(4);
        populate();
        unsetCursor();
        statusBar()->showMessage(QString("Found %1 large files, biggest first").arg(paths.size()), 8000);
    }

    void exportBrewfile() {
        if (g_brew.isEmpty()) return;
        QString f = QFileDialog::getSaveFileName(this, "Export Brewfile", QDir::homePath() + "/Brewfile");
        if (f.isEmpty()) return;
        QString o;
        bool ok = runOk(g_brew, {"bundle", "dump", "--force", "--file=" + f}, &o);
        statusBar()->showMessage(ok ? "Saved " + f : "Export failed: " + o.left(200), 8000);
    }

    void showAbout() {
        QMessageBox::about(this, "About Tidy",
            "<h2>Tidy 1.1</h2><p>A visual cleaner for Homebrew packages, apps and system junk on macOS.</p>"
            "<p>• See what each package does and what it connects to<br>"
            "• Remove apps together with everything they leave behind<br>"
            "• Clear caches, find large files, export a Brewfile<br>"
            "• Every removal is confirmed first, and app files go to the Trash.</p>"
            "<p style='color:gray'>Built with C++17 and Qt " + QString(qVersion()) + ".</p>");
    }

    void runBulk(const QList<QStringList> &cmds) {
        if (g_brew.isEmpty()) return;
        setCursor(Qt::WaitCursor);
        QString all;
        for (auto &c : cmds) { QString o; runOk(g_brew, c, &o); all += o + "\n"; }
        unsetCursor();
        loadAll();
        QMessageBox::information(this, "Tidy", all.trimmed().right(1500));
    }
};

int main(int argc, char **argv) {
    qputenv("PATH", "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin");
    qputenv("HOMEBREW_NO_AUTO_UPDATE", "1");
    qputenv("HOMEBREW_NO_INSTALL_CLEANUP", "1");
    QApplication app(argc, argv);
    app.setApplicationName("Tidy");
    app.setApplicationVersion("1.1");
    app.setStyleSheet(R"(
        QListWidget { border: none; background: transparent; outline: 0; }
        QListWidget::item { padding: 6px 10px; border-radius: 6px; }
        QListWidget::item:selected { background: #0a84ff; color: white; }
        QLineEdit { border: 1px solid rgba(128,128,128,110); border-radius: 8px; padding: 6px 10px; }
        QPushButton#danger { background: #ff453a; color: white; border: none; border-radius: 8px;
                             padding: 9px 20px; font-weight: 600; }
        QPushButton#danger:disabled { background: rgba(128,128,128,110); }
        QToolBar { border: none; spacing: 8px; padding: 4px; }
        QListWidget#side { font-size: 14px; }
        QListWidget#side::item { padding: 9px 12px; }
        QFrame#card { background: rgba(128,128,128,26); border-radius: 12px; }
        QSplitter::handle { background: rgba(128,128,128,50); }
        QComboBox, QPushButton { border: 1px solid rgba(128,128,128,110); border-radius: 8px; padding: 5px 12px; }
    )");
    for (const char *p : {"/opt/homebrew/bin/brew", "/usr/local/bin/brew"})
        if (QFile::exists(p)) { g_brew = p; break; }
    if (!g_brew.isEmpty()) g_prefix = g_brew.left(g_brew.size() - 9);  // strip "/bin/brew"
    MainWindow w;
    w.show();
    QTimer::singleShot(50, &w, &MainWindow::loadAll);
    return app.exec();
}

#include "main.moc"