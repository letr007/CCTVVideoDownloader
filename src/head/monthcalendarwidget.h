#pragma once

#include <QCalendarWidget>
#include <QList>

class QLabel;
class QKeyEvent;
class QResizeEvent;
class QShowEvent;
class QToolButton;
class QWidget;

class MonthCalendarWidget final : public QCalendarWidget
{
public:
    explicit MonthCalendarWidget(const QDate& minimumDate,
        const QDate& maximumDate,
        QWidget* parent = nullptr);

    void showYear(int year);
    int displayedYear() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void updateMonths();
    void moveSelectionByMonths(int months);
    void selectMonth(int month);

    QWidget* m_panel = nullptr;
    QLabel* m_yearLabel = nullptr;
    QToolButton* m_previousYearButton = nullptr;
    QToolButton* m_nextYearButton = nullptr;
    QList<QToolButton*> m_monthButtons;
    int m_displayedYear = 0;
};
