#include "ui/SettingsDialog.h"

#include "core/Settings.h"
#include "platform/Autostart.h"
#include "platform/HotkeyEdit.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

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
    QSpinBox *annotationWidth = nullptr;
    QDoubleSpinBox *pinOpacity = nullptr;
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

    setWindowTitle(QStringLiteral("TaskPin 设置"));
    resize(620, 600);
    setWindowFlag(Qt::WindowStaysOnTopHint, true);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildCardPage(), QStringLiteral("卡片"));
    tabs->addTab(buildHotkeyPage(), QStringLiteral("热键"));
    tabs->addTab(buildSnipPage(), QStringLiteral("截图与贴图"));
    tabs->addTab(buildAiPage(), QStringLiteral("AI"));
    tabs->addTab(buildMiscPage(), QStringLiteral("其它"));

    auto *buttons = new QDialogButtonBox(this);
    auto *save = buttons->addButton(QStringLiteral("保存"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);

    connect(save, &QPushButton::clicked, this, [this] {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
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
        const QColor c = QColorDialog::getColor(m_cardBg, this, QStringLiteral("选择卡片背景色"));
        if (c.isValid()) {
            m_cardBg = c;
            styleColorButton(bg, c);
        }
    });
    form->addRow(QStringLiteral("卡片背景色"), bg);

    auto *fg = new QPushButton(page);
    styleColorButton(fg, m_cardFg);
    connect(fg, &QPushButton::clicked, this, [this, fg] {
        const QColor c = QColorDialog::getColor(m_cardFg, this, QStringLiteral("选择卡片文字色"));
        if (c.isValid()) {
            m_cardFg = c;
            styleColorButton(fg, c);
        }
    });
    form->addRow(QStringLiteral("卡片文字色"), fg);

    auto *resetPos = new QPushButton(QStringLiteral("恢复自动位置"), page);
    connect(resetPos, &QPushButton::clicked, this, [] {
        Settings::instance().cardX = -1;
        Settings::instance().cardY = -1;
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
        edit->setChord(Settings::instance().chordFor(action.first));
        m_hotkeyEdits[action.first] = edit;
        form->addRow(action.second, edit);
    }
    layout->addLayout(form);
    layout->addWidget(hintLabel(QStringLiteral(
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

    m_ui->annotationWidth = new QSpinBox(page);
    m_ui->annotationWidth->setRange(1, 40);
    m_ui->annotationWidth->setValue(qBound(1, s.annotationWidth, 40));
    form->addRow(QStringLiteral("默认线宽"), m_ui->annotationWidth);

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

    return page;
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
    form->addRow(QStringLiteral("接口地址"), m_ui->aiBaseUrl);

    m_ui->aiKey = new QLineEdit(s.aiApiKey, page);
    m_ui->aiKey->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("API Key"), m_ui->aiKey);

    m_ui->aiModel = new QLineEdit(s.aiModel, page);
    form->addRow(QStringLiteral("模型"), m_ui->aiModel);

    form->addRow(QString(), hintLabel(QStringLiteral(
        "OpenAI 兼容接口（如 https://api.deepseek.com/v1）。关闭时一切都在本地完成；\n"
        "开启后只把每次识别出的文本发出去，不发截图。")));
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

void SettingsDialog::apply()
{
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

    for (auto it = m_hotkeyEdits.begin(); it != m_hotkeyEdits.end(); ++it) s.setChord(it.key(), it.value()->chord());

    s.snipDelay = m_ui->snipDelay->value();
    s.annotationColor = m_annotationColor;
    s.annotationWidth = m_ui->annotationWidth->value();
    s.autoSave = m_ui->autoSave->isChecked();
    s.autoSaveDir = m_ui->autoSaveDir->text().trimmed();
    s.namePattern = m_ui->namePattern->text().trimmed();
    s.historyLimit = m_ui->historyLimit->value();
    s.pinOpacity = m_ui->pinOpacity->value();

    s.aiEnabled = m_ui->aiEnabled->isChecked();
    s.aiBaseUrl = m_ui->aiBaseUrl->text().trimmed();
    s.aiApiKey = m_ui->aiKey->text().trimmed();
    s.aiModel = m_ui->aiModel->text().trimmed();

    s.autostart = m_ui->autostart->isChecked();
    Autostart::setEnabled(s.autostart, QCoreApplication::applicationFilePath());

    s.save();
    emit saved();
}
