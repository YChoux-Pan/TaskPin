#include "ui/SettingsDialog.h"
#include "ui/TranslationSettingsPage.h"

#include "core/Settings.h"
#include "platform/Autostart.h"
#include "platform/HotkeyEdit.h"
#include "platform/HotkeyText.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QFile>
#include <QSaveFile>

#ifndef TASKPIN_VERSION
#define TASKPIN_VERSION "0.2.0"
#endif

namespace {

void styleColorButton(QPushButton *button, const QColor &color)
{
    const QString fg = color.lightness() > 140 ? QStringLiteral("#111111") : QStringLiteral("#F2F5F8");
    button->setText(color.name().toUpper());
    button->setStyleSheet(QStringLiteral("QPushButton { background:%1; color:%2; border:1px solid #55606e;"
                                         " border-radius:6px; padding:4px 10px; }")
                              .arg(color.name(), fg));
}

QLabel *hintLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setStyleSheet(QStringLiteral("color:#8A92A0; font-size:11px;"));
    label->setWordWrap(true);
    return label;
}

QSlider *addWidthSlider(QFormLayout *form, const QString &title, const QString &name,
                        int minimum, int maximum, int current)
{
    auto *slider = new QSlider(Qt::Horizontal, form->parentWidget());
    slider->setObjectName(name);
    slider->setAccessibleName(title);
    slider->setRange(minimum, maximum);
    slider->setValue(current);
    auto *value = new QLabel(form->parentWidget());
    value->setObjectName(name + QStringLiteral("Value"));
    value->setMinimumWidth(value->fontMetrics().horizontalAdvance(QStringLiteral("%1 px").arg(maximum)));
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    const auto showValue = [value](int pixels) { value->setText(QStringLiteral("%1 px").arg(pixels)); };
    showValue(slider->value());
    QObject::connect(slider, &QSlider::valueChanged, value, showValue);

    auto *row = new QHBoxLayout();
    row->addWidget(slider, 1);
    row->addWidget(value);
    auto *label = new QLabel(title, form->parentWidget());
    label->setBuddy(slider);
    form->addRow(label, row);
    return slider;
}

}   // namespace

struct SettingsDialog::Ui {
    QComboBox *position = nullptr;
    QSpinBox *cardWidth = nullptr;
    QSpinBox *visibleRows = nullptr;
    QSlider *cardOpacity = nullptr;
    QCheckBox *alwaysOnTop = nullptr;
    QCheckBox *showCarryMark = nullptr;
    QDoubleSpinBox *snipDelay = nullptr;
    QCheckBox *autoSave = nullptr;
    QLineEdit *autoSaveDir = nullptr;
    QLineEdit *namePattern = nullptr;
    QSpinBox *historyLimit = nullptr;
    QSlider *annotationWidth = nullptr;
    QDoubleSpinBox *pinOpacity = nullptr;
    QSpinBox *pinShadow = nullptr;
    QSlider *pinBorderWidth = nullptr;
    QCheckBox *aiEnabled = nullptr;
    QLineEdit *aiBaseUrl = nullptr;
    QLineEdit *aiKey = nullptr;
    QLineEdit *aiModel = nullptr;
    QCheckBox *autostart = nullptr;
};

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent), m_ui(new Ui)
{
    const auto &s = Settings::instance();
    m_cardBg = s.cardBg;
    m_cardFg = s.cardFg;
    m_annotationColor = s.annotationColor;
    m_pinBorderColor = s.pinBorderColor;

    setWindowTitle(QStringLiteral("TaskPin 设置"));
    resize(620, 600);
    setWindowFlag(Qt::WindowStaysOnTopHint,true);

    auto *tabs = new QTabWidget(this);m_tabs=tabs;
    tabs->addTab(buildCardPage(), QStringLiteral("卡片"));
    tabs->addTab(buildHotkeyPage(), QStringLiteral("热键"));
    tabs->addTab(buildSnipPage(), QStringLiteral("截图与贴图"));
    tabs->addTab(buildAiPage(), QStringLiteral("AI"));
    m_translation=new TranslationSettingsPage(this);
    tabs->addTab(m_translation, QStringLiteral("截图翻译"));
    auto syncConnection=[this] {m_translation->setConnection(m_ui->aiBaseUrl->text(),m_ui->aiModel->text(),m_ui->aiKey->text());};
    for(auto *field:{m_ui->aiBaseUrl,m_ui->aiModel,m_ui->aiKey})connect(field,&QLineEdit::textChanged,this,syncConnection);
    syncConnection();
    connect(m_translation,&TranslationSettingsPage::editAiRequested,this,[this] {m_tabs->setCurrentIndex(3);});
    tabs->addTab(buildMiscPage(), QStringLiteral("其它"));

    auto *buttons = new QDialogButtonBox(this);
    auto *save = buttons->addButton(QStringLiteral("保存"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    m_saveError=new QLabel(this);m_saveError->setObjectName("settingsSaveError");
    m_saveError->setWordWrap(true);m_saveError->setTextFormat(Qt::PlainText);m_saveError->setStyleSheet("color:#c74646");layout->addWidget(m_saveError);
    layout->addWidget(buttons);

    connect(save, &QPushButton::clicked, this, [this] {
        if(apply())accept();
    });
    connect(m_translation,&TranslationSettingsPage::saveReady,this,[this] {
        if(apply())accept();
    });
    connect(m_translation,&TranslationSettingsPage::validationBusyChanged,save,[save](bool busy) {
        save->setEnabled(!busy);
        save->setText(busy?QStringLiteral("正在验证模型…"):QStringLiteral("保存"));
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(this,&QDialog::finished,m_translation,&TranslationSettingsPage::cancelTest);
    connect(m_ui->cardWidth, qOverload<int>(&QSpinBox::valueChanged), this, &SettingsDialog::emitPreview);
    connect(m_ui->visibleRows, qOverload<int>(&QSpinBox::valueChanged), this, &SettingsDialog::emitPreview);
    connect(m_ui->cardOpacity, &QSlider::valueChanged, this, &SettingsDialog::emitPreview);
    connect(m_ui->showCarryMark, &QCheckBox::toggled, this, &SettingsDialog::emitPreview);
}

void SettingsDialog::showTranslationPage(){m_tabs->setCurrentWidget(m_translation);}

SettingsDialog::~SettingsDialog() { delete m_ui; }

void SettingsDialog::emitPreview()
{
    emit cardPreviewChanged({m_cardBg, m_cardFg, m_ui->cardOpacity->value() / 100.0,
                             m_ui->cardWidth->value(), m_ui->visibleRows->value(),
                             m_ui->showCarryMark->isChecked()});
}

void SettingsDialog::chooseColor(QPushButton *button, QColor &value, bool preview)
{
    const QColor before = value;
    QColorDialog picker(value, this);
    picker.setWindowTitle(QStringLiteral("选择颜色 · 即时预览"));
    picker.setOption(QColorDialog::DontUseNativeDialog);
    connect(&picker, &QColorDialog::currentColorChanged, this, [this, button, &value, preview](const QColor &c) {
        if (!c.isValid()) return;
        value = c;
        styleColorButton(button, c);
        if (preview) emitPreview();
    });
    if (picker.exec() != QDialog::Accepted) {
        value = before;
        styleColorButton(button, before);
        if (preview) emitPreview();
    }
}

QWidget *SettingsDialog::buildCardPage()
{
    const auto &s = Settings::instance();
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    m_ui->position = new QComboBox(page);
    m_ui->position->addItems({QStringLiteral("右上角"), QStringLiteral("左上角"), QStringLiteral("右下角"),
                              QStringLiteral("左下角")});
    m_ui->position->setCurrentIndex(s.cardPosition == QLatin1String("TopLeft")       ? 1
                                    : s.cardPosition == QLatin1String("BottomRight") ? 2
                                    : s.cardPosition == QLatin1String("BottomLeft")  ? 3
                                                                                     : 0);
    form->addRow(QStringLiteral("默认位置"), m_ui->position);

    m_ui->cardWidth = new QSpinBox(page);
    m_ui->cardWidth->setRange(280, 640);
    m_ui->cardWidth->setSingleStep(20);
    m_ui->cardWidth->setValue(qBound(280, s.cardWidth, 640));
    form->addRow(QStringLiteral("卡片宽度 (dip)"), m_ui->cardWidth);

    m_ui->visibleRows = new QSpinBox(page);
    m_ui->visibleRows->setRange(3, 14);
    m_ui->visibleRows->setValue(qBound(3, s.visibleRows, 14));
    form->addRow(QStringLiteral("默认显示条数"), m_ui->visibleRows);

    m_ui->cardOpacity = new QSlider(Qt::Horizontal, page);
    m_ui->cardOpacity->setRange(40, 100);
    m_ui->cardOpacity->setValue(qRound(qBound(0.4, s.opacity, 1.0) * 100));
    form->addRow(QStringLiteral("背景不透明度 %"), m_ui->cardOpacity);

    m_ui->alwaysOnTop =
        new QCheckBox(QStringLiteral("卡片全局置顶（取消勾选 = 贴在桌面层：壁纸上方，会被普通窗口盖住）"), page);
    m_ui->alwaysOnTop->setChecked(s.alwaysOnTop);
    form->addRow(QString(), m_ui->alwaysOnTop);

    m_ui->showCarryMark = new QCheckBox(QStringLiteral("在任务右侧显示顺延次数（如 ×3）"), page);
    m_ui->showCarryMark->setChecked(s.showCarryMark);
    form->addRow(QString(), m_ui->showCarryMark);

    auto *bg = new QPushButton(page);
    styleColorButton(bg, m_cardBg);
    connect(bg, &QPushButton::clicked, this, [this, bg] {
        chooseColor(bg, m_cardBg, true);
    });
    form->addRow(QStringLiteral("卡片背景色"), bg);

    auto *fg = new QPushButton(page);
    styleColorButton(fg, m_cardFg);
    connect(fg, &QPushButton::clicked, this, [this, fg] {
        chooseColor(fg, m_cardFg, true);
    });
    form->addRow(QStringLiteral("卡片文字色"), fg);

    auto *resetPos = new QPushButton(QStringLiteral("恢复自动位置"), page);
    connect(resetPos, &QPushButton::clicked, this, [this, resetPos] {
        m_resetPosition = true;
        resetPos->setText(QStringLiteral("保存后恢复自动位置"));
    });
    form->addRow(QString(), resetPos);
    return page;
}

QWidget *SettingsDialog::buildHotkeyPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    auto *form = new QFormLayout();
    for (const auto &action : Settings::hotkeyActions()) {
        auto *edit = new HotkeyEdit(page);
        edit->setObjectName("hotkey_" + action.first);
        edit->setChord(Settings::instance().chordFor(action.first));
        m_hotkeyEdits[action.first] = edit;
        form->addRow(action.second, edit);
    }
    layout->addLayout(form);
    layout->addWidget(hintLabel(QStringLiteral(
        "默认 F1 截图、F3 贴图、Shift+F3 显隐全部贴图。F1框选后，贴图键会贴住当前选区与标注；平时贴剪贴板内容。\n"
        "截图翻译快捷键只在 F1 框选编辑器中生效，默认 Ctrl+T；Ctrl+P 仍可贴住选区。\n"
        "点进输入框后直接按下组合键即可录制；Backspace 清除（恢复默认）。\n"
        "被其它软件占用时会自动降级到备用键，并把实际生效的按键写进日志。")));

    auto *reset = new QPushButton(QStringLiteral("全部恢复默认"), page);
    connect(reset, &QPushButton::clicked, this, [this] {
        for (auto it = m_hotkeyEdits.begin(); it != m_hotkeyEdits.end(); ++it)
            it.value()->setChord(Settings::defaultChord(it.key()));
    });
    layout->addWidget(reset, 0, Qt::AlignLeft);
    layout->addStretch(1);
    return page;
}

QWidget *SettingsDialog::buildSnipPage()
{
    const auto &s = Settings::instance();
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    m_ui->snipDelay = new QDoubleSpinBox(page);
    m_ui->snipDelay->setRange(0.0, 10.0);
    m_ui->snipDelay->setSingleStep(0.5);
    m_ui->snipDelay->setDecimals(1);
    m_ui->snipDelay->setSuffix(QStringLiteral(" 秒"));
    m_ui->snipDelay->setValue(qBound(0.0, s.snipDelay, 10.0));
    form->addRow(QStringLiteral("截图延迟"), m_ui->snipDelay);

    auto *color = new QPushButton(page);
    styleColorButton(color, m_annotationColor);
    connect(color, &QPushButton::clicked, this, [this, color] {
        const QColor c = QColorDialog::getColor(m_annotationColor, this, QStringLiteral("默认标注颜色"));
        if (c.isValid()) {
            m_annotationColor = c;
            styleColorButton(color, c);
        }
    });
    form->addRow(QStringLiteral("默认标注颜色"), color);

    m_ui->annotationWidth = addWidthSlider(form, QStringLiteral("默认线宽"), QStringLiteral("annotationWidth"),
                                           1, 40, s.annotationWidth);

    m_ui->autoSave = new QCheckBox(QStringLiteral("每次截图自动另存一份"), page);
    m_ui->autoSave->setChecked(s.autoSave);
    form->addRow(QString(), m_ui->autoSave);

    auto *dirRow = new QWidget(page);
    auto *dirLayout = new QHBoxLayout(dirRow);
    dirLayout->setContentsMargins(0, 0, 0, 0);
    m_ui->autoSaveDir = new QLineEdit(s.autoSaveDir, dirRow);
    m_ui->autoSaveDir->setPlaceholderText(QStringLiteral("留空 = 图片\\TaskPin"));
    auto *browse = new QPushButton(QStringLiteral("浏览…"), dirRow);
    dirLayout->addWidget(m_ui->autoSaveDir);
    dirLayout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择自动保存目录"),
                                                             m_ui->autoSaveDir->text());
        if (!dir.isEmpty()) m_ui->autoSaveDir->setText(QDir::toNativeSeparators(dir));
    });
    form->addRow(QStringLiteral("自动保存目录"), dirRow);
    form->addRow(QString(), hintLabel(QStringLiteral(
        "Ctrl+Shift+S 保存到此目录；保存或另存为成功后，完整文件路径会复制到剪贴板。")));

    m_ui->namePattern = new QLineEdit(s.namePattern, page);
    form->addRow(QStringLiteral("文件名规则"), m_ui->namePattern);
    form->addRow(QString(), hintLabel(QStringLiteral("可用占位符：{yyyy} {yy} {MM} {dd} {HH} {mm} {ss} {fff}")));

    m_ui->historyLimit = new QSpinBox(page);
    m_ui->historyLimit->setRange(5, 500);
    m_ui->historyLimit->setValue(qBound(5, s.historyLimit, 500));
    form->addRow(QStringLiteral("历史保留张数"), m_ui->historyLimit);

    m_ui->pinOpacity = new QDoubleSpinBox(page);
    m_ui->pinOpacity->setRange(0.1, 1.0);
    m_ui->pinOpacity->setSingleStep(0.05);
    m_ui->pinOpacity->setDecimals(2);
    m_ui->pinOpacity->setValue(qBound(0.1, s.pinOpacity, 1.0));
    form->addRow(QStringLiteral("新贴图不透明度"), m_ui->pinOpacity);
    m_ui->pinShadow = new QSpinBox(page);
    m_ui->pinShadow->setRange(0, 100);
    m_ui->pinShadow->setSuffix(" %");
    m_ui->pinShadow->setSpecialValueText(QStringLiteral("关闭阴影"));
    m_ui->pinShadow->setValue(s.pinShadowStrength);
    form->addRow(QStringLiteral("贴图阴影强度"), m_ui->pinShadow);
    m_ui->pinBorderWidth = addWidthSlider(form, QStringLiteral("贴图边框宽度"), QStringLiteral("pinBorderWidth"),
                                          0, 8, s.pinBorderWidth);
    auto *borderColor = new QPushButton(page);
    styleColorButton(borderColor, m_pinBorderColor);
    connect(borderColor, &QPushButton::clicked, this, [this, borderColor] {
        chooseColor(borderColor, m_pinBorderColor, false);
    });
    form->addRow(QStringLiteral("贴图边框颜色"), borderColor);
    form->addRow(QString(), hintLabel(QStringLiteral("阴影设为 0、边框设为 0 可完全关闭。保存后更新当前及新贴图；单张贴图也可通过右键调整。")));
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(page);
    return scroll;
}

QWidget *SettingsDialog::buildAiPage()
{
    const auto &s = Settings::instance();
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    m_ui->aiEnabled = new QCheckBox(QStringLiteral("启用 AI 归纳（把 OCR 长文压成一句话标题）"), page);
    m_ui->aiEnabled->setChecked(s.aiEnabled);
    form->addRow(QString(), m_ui->aiEnabled);

    m_ui->aiBaseUrl = new QLineEdit(s.aiBaseUrl, page);
    m_ui->aiBaseUrl->setObjectName("aiEndpoint");
    form->addRow(QStringLiteral("接口地址"), m_ui->aiBaseUrl);

    m_ui->aiKey = new QLineEdit(s.aiApiKey, page);
    m_ui->aiKey->setObjectName("aiApiKey");
    m_ui->aiKey->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("API Key"), m_ui->aiKey);

    m_ui->aiModel = new QLineEdit(s.aiModel, page);
    m_ui->aiModel->setObjectName("aiModel");
    form->addRow(QStringLiteral("模型"), m_ui->aiModel);

    form->addRow(QString(), hintLabel(QStringLiteral(
        "便签归纳与截图翻译共用此连接。上方开关只控制便签标题归纳。\n"
        "密码框圆点代表已填写，保存后重新打开仍保留。密钥沿用原 AI 模块的本机存储。")));
    auto *translation = new QPushButton(QStringLiteral("截图翻译 · 独立服务与隐私设置…"),page);
    translation->setObjectName("openTranslationSettings");
    connect(translation,&QPushButton::clicked,this,&SettingsDialog::showTranslationPage);
    form->addRow(QStringLiteral("截图翻译"),translation);
    return page;
}

QWidget *SettingsDialog::buildMiscPage()
{
    const auto &s = Settings::instance();
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    m_ui->autostart = new QCheckBox(QStringLiteral("开机自动启动（当前用户运行项）"), page);
    m_ui->autostart->setChecked(Autostart::isEnabled() || s.autostart);
    form->addRow(QString(), m_ui->autostart);

    auto *dataDir = new QPushButton(QStringLiteral("打开数据目录"), page);
    connect(dataDir, &QPushButton::clicked, this,
            [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Settings::dataDir())); });
    form->addRow(QStringLiteral("数据目录"), dataDir);

    auto *log = new QPushButton(QStringLiteral("打开日志"), page);
    connect(log, &QPushButton::clicked, this,
            [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Settings::logPath())); });
    form->addRow(QStringLiteral("运行日志"), log);

    auto *version = new QLabel(QStringLiteral("TaskPin Qt %1").arg(QStringLiteral(TASKPIN_VERSION)), page);
    version->setStyleSheet(QStringLiteral("color:#8A92A0;"));
    form->addRow(QStringLiteral("版本"), version);

    return page;
}

bool SettingsDialog::apply()
{
    if (!validateHotkeys()) return false;
    const auto previous=Settings::instance();
    struct PriorFile { QString path; bool exists; QByteArray bytes; };
    QList<PriorFile> priorFiles;
    for (const auto &name : {QStringLiteral("translation.json"), QStringLiteral("ocr.json")}) {
        PriorFile prior{Settings::dataDir() + "/" + name, false, {}};
        QFile file(prior.path);
        prior.exists = file.exists();
        if (prior.exists) {
            if (!file.open(QIODevice::ReadOnly)) {
                m_saveError->setText(QStringLiteral("无法读取现有翻译或 OCR 设置，更改未保存。"));
                return false;
            }
            prior.bytes = file.readAll();
        }
        priorFiles.append(prior);
    }
    if(!m_translation->save()){showTranslationPage();return false;}
    auto &s = Settings::instance();
    static const QStringList positions{QStringLiteral("TopRight"), QStringLiteral("TopLeft"),
                                       QStringLiteral("BottomRight"), QStringLiteral("BottomLeft")};
    s.cardPosition = positions.value(qBound(0, m_ui->position->currentIndex(), 3), QStringLiteral("TopRight"));
    s.cardWidth = m_ui->cardWidth->value();
    s.visibleRows = m_ui->visibleRows->value();
    s.opacity = m_ui->cardOpacity->value() / 100.0;
    s.alwaysOnTop = m_ui->alwaysOnTop->isChecked();
    s.showCarryMark = m_ui->showCarryMark->isChecked();
    s.cardBg = m_cardBg;
    s.cardFg = m_cardFg;
    if (m_resetPosition) { s.cardX = -1; s.cardY = -1; }

    for (auto it = m_hotkeyEdits.begin(); it != m_hotkeyEdits.end(); ++it) s.setChord(it.key(), it.value()->chord());

    s.snipDelay = m_ui->snipDelay->value();
    s.annotationColor = m_annotationColor;
    s.annotationWidth = m_ui->annotationWidth->value();
    s.autoSave = m_ui->autoSave->isChecked();
    s.autoSaveDir = m_ui->autoSaveDir->text().trimmed();
    s.namePattern = m_ui->namePattern->text().trimmed();
    s.historyLimit = m_ui->historyLimit->value();
    s.pinOpacity = m_ui->pinOpacity->value();
    s.pinShadowStrength = m_ui->pinShadow->value();
    s.pinBorderWidth = m_ui->pinBorderWidth->value();
    s.pinBorderColor = m_pinBorderColor;

    s.aiEnabled = m_ui->aiEnabled->isChecked();
    s.aiBaseUrl = m_ui->aiBaseUrl->text().trimmed();
    s.aiApiKey = m_ui->aiKey->text().trimmed();
    s.aiModel = m_ui->aiModel->text().trimmed();

    s.autostart = m_ui->autostart->isChecked();

    QString error;
    if(!s.save(&error)) {
        s=previous;bool restored=true;
        for (const auto &prior : priorFiles) {
            if (prior.exists) {
                QSaveFile restore(prior.path);
                const bool fileRestored = restore.open(QIODevice::WriteOnly)
                    && restore.write(prior.bytes) == prior.bytes.size() && restore.commit();
                restored = fileRestored && restored;
            } else if (QFile::exists(prior.path)) {
                restored = QFile::remove(prior.path) && restored;
            }
        }
        m_saveError->setText(error+(restored?QStringLiteral("；更改未保存，草稿仍保留，请修复目录权限后重试。"):QStringLiteral("；翻译或 OCR 设置回滚失败，请检查磁盘后重新确认设置。")));
        return false;
    }
    // AppHost applies OS integration after saved(), including automation isolation.
    emit saved();
    return true;
}

bool SettingsDialog::validateHotkeys()
{
    QMap<QString, QString> used;
    QMap<QString, QString> globalFallbacks;
    for (const auto &action : Settings::hotkeyActions()) {
        if (action.first == "editorTranslate") continue;
        const auto fallback = HotkeyText::parse(Settings::fallbackChord(action.first));
        if (fallback.valid()) globalFallbacks.insert(fallback.text, action.second);
    }
    const QStringList reserved{
        "V", "R", "O", "A", "L", "P", "H", "T", "N", "M", "B", "E",
        "Space", "Enter", "Esc", "Delete", "Backspace", "Ctrl+P", "Ctrl+R",
        "Ctrl+Z", "Ctrl+Shift+Z", "Ctrl+Y", "Ctrl+S", "Ctrl+Shift+S",
        "Left", "Right", "Up", "Down", "Shift+Left", "Shift+Right", "Shift+Up", "Shift+Down",
        "Alt+Left", "Alt+Right", "Alt+Up", "Alt+Down"
    };
    for (const auto &action : Settings::hotkeyActions()) {
        const auto *edit = m_hotkeyEdits.value(action.first);
        const QString value = edit->chord().isEmpty() ? Settings::defaultChord(action.first) : edit->chord();
        const auto chord = HotkeyText::parse(value);
        QString error;
        if (!chord.valid()) {
            error = QStringLiteral("“%1”的快捷键无效，请重新录制。").arg(action.second);
        } else if (used.contains(chord.text)) {
            error = QStringLiteral("“%1”与“%2”使用相同快捷键 %3，请更换后保存。")
                .arg(action.second, used.value(chord.text), chord.text);
        } else if (action.first == "editorTranslate" && globalFallbacks.contains(chord.text)) {
            error = QStringLiteral("“%1”的快捷键 %2 与“%3”的备用全局快捷键冲突，请更换后保存。")
                .arg(action.second, chord.text, globalFallbacks.value(chord.text));
        } else if (reserved.contains(chord.text)
                   && !(action.first == "pin" && chord.text == "Ctrl+P")) {
            error = QStringLiteral("“%1”的快捷键 %2 与截图编辑器内置操作冲突，请更换后保存。")
                .arg(action.second, chord.text);
        }
        if (!error.isEmpty()) {
            m_saveError->setText(error);
            m_tabs->setCurrentIndex(1);
            return false;
        }
        used.insert(chord.text, action.second);
    }
    m_saveError->clear();
    return true;
}
