#include "../include/monthcalendarwidget.h"

#include <QAbstractItemView>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

MonthCalendarWidget::MonthCalendarWidget(const QDate& minimumDate,
    const QDate& maximumDate,
    QWidget* parent)
    : QCalendarWidget(parent)
    , m_displayedYear(maximumDate.year())
{
    setObjectName(QStringLiteral("monthCalendar"));
    setDateRange(minimumDate, maximumDate);
    setFocusPolicy(Qt::StrongFocus);
    setNavigationBarVisible(false);
    setGridVisible(false);

    if (auto* calendarView = findChild<QAbstractItemView*>(
        QStringLiteral("qt_calendar_calendarview"))) {
        calendarView->hide();
    }

    m_panel = new QWidget(this);
    m_panel->setObjectName(QStringLiteral("monthPickerPanel"));
    auto* panelLayout = new QVBoxLayout(m_panel);
    panelLayout->setContentsMargins(12, 12, 12, 12);
    panelLayout->setSpacing(10);

    auto* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    m_previousYearButton = new QToolButton(m_panel);
    m_previousYearButton->setObjectName(QStringLiteral("monthPickerPreviousYear"));
    m_previousYearButton->setText(QStringLiteral("‹"));
    m_previousYearButton->setToolTip(QStringLiteral("上一年"));
    m_previousYearButton->setFocusPolicy(Qt::NoFocus);
    headerLayout->addWidget(m_previousYearButton);

    m_yearLabel = new QLabel(m_panel);
    m_yearLabel->setObjectName(QStringLiteral("monthPickerYear"));
    m_yearLabel->setAlignment(Qt::AlignCenter);
    headerLayout->addWidget(m_yearLabel, 1);

    m_nextYearButton = new QToolButton(m_panel);
    m_nextYearButton->setObjectName(QStringLiteral("monthPickerNextYear"));
    m_nextYearButton->setText(QStringLiteral("›"));
    m_nextYearButton->setToolTip(QStringLiteral("下一年"));
    m_nextYearButton->setFocusPolicy(Qt::NoFocus);
    headerLayout->addWidget(m_nextYearButton);
    panelLayout->addLayout(headerLayout);

    auto* monthsLayout = new QGridLayout();
    monthsLayout->setContentsMargins(0, 0, 0, 0);
    monthsLayout->setHorizontalSpacing(8);
    monthsLayout->setVerticalSpacing(8);
    for (int month = 1; month <= 12; ++month) {
        auto* button = new QToolButton(m_panel);
        button->setObjectName(QStringLiteral("monthPickerMonth%1").arg(month));
        button->setProperty("monthButton", true);
        button->setText(QStringLiteral("%1月").arg(month));
        button->setFocusPolicy(Qt::NoFocus);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        connect(button, &QToolButton::clicked, this, [this, month]() {
            selectMonth(month);
        });
        monthsLayout->addWidget(button, (month - 1) / 4, (month - 1) % 4);
        m_monthButtons.append(button);
    }
    panelLayout->addLayout(monthsLayout);

    connect(m_previousYearButton, &QToolButton::clicked, this, [this]() {
        showYear(m_displayedYear - 1);
    });
    connect(m_nextYearButton, &QToolButton::clicked, this, [this]() {
        showYear(m_displayedYear + 1);
    });

    updateMonths();
}

void MonthCalendarWidget::showYear(int year)
{
    m_displayedYear = qBound(minimumDate().year(), year, maximumDate().year());
    updateMonths();
}

int MonthCalendarWidget::displayedYear() const
{
    return m_displayedYear;
}

QSize MonthCalendarWidget::sizeHint() const
{
    return QSize(320, 218);
}

QSize MonthCalendarWidget::minimumSizeHint() const
{
    return sizeHint();
}

void MonthCalendarWidget::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Left:
        moveSelectionByMonths(-1);
        break;
    case Qt::Key_Right:
        moveSelectionByMonths(1);
        break;
    case Qt::Key_Up:
        moveSelectionByMonths(-4);
        break;
    case Qt::Key_Down:
        moveSelectionByMonths(4);
        break;
    case Qt::Key_PageUp:
        moveSelectionByMonths(-12);
        break;
    case Qt::Key_PageDown:
        moveSelectionByMonths(12);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        Q_EMIT activated(selectedDate());
        break;
    default:
        QCalendarWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

void MonthCalendarWidget::resizeEvent(QResizeEvent* event)
{
    QCalendarWidget::resizeEvent(event);
    m_panel->setGeometry(rect());
    m_panel->raise();
}

void MonthCalendarWidget::showEvent(QShowEvent* event)
{
    QCalendarWidget::showEvent(event);
    m_displayedYear = selectedDate().year();
    updateMonths();
    m_panel->setGeometry(rect());
    m_panel->raise();
    setFocus(Qt::PopupFocusReason);
}

void MonthCalendarWidget::updateMonths()
{
    m_yearLabel->setText(QStringLiteral("%1年").arg(m_displayedYear));
    m_previousYearButton->setEnabled(m_displayedYear > minimumDate().year());
    m_nextYearButton->setEnabled(m_displayedYear < maximumDate().year());

    const QDate selectedMonth(selectedDate().year(), selectedDate().month(), 1);
    const QDate minimumMonth(minimumDate().year(), minimumDate().month(), 1);
    const QDate maximumMonth(maximumDate().year(), maximumDate().month(), 1);
    for (int index = 0; index < m_monthButtons.size(); ++index) {
        QToolButton* button = m_monthButtons.at(index);
        const QDate month(m_displayedYear, index + 1, 1);
        button->setEnabled(month >= minimumMonth && month <= maximumMonth);
        button->setProperty("selected", month == selectedMonth);
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
    }
}

void MonthCalendarWidget::moveSelectionByMonths(int months)
{
    const QDate candidate = selectedDate().addMonths(months);
    if (candidate < minimumDate() || candidate > maximumDate()) {
        return;
    }

    setSelectedDate(candidate);
    m_displayedYear = candidate.year();
    updateMonths();
}

void MonthCalendarWidget::selectMonth(int month)
{
    const QDate date(m_displayedYear, month, 1);
    if (date < minimumDate() || date > maximumDate()) {
        return;
    }

    setSelectedDate(date);
    Q_EMIT activated(date);
}
