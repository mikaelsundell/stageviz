// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "pythoneditor.h"

#include <QPainter>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextBlock>

namespace stageviz {

class PythonEditorPrivate {
public:
    void init();
    int lineNumberAreaWidth() const;
    void updateLineNumberAreaWidth(int);
    void updateLineNumberArea(const QRect& rect, int dy);
    void highlightCurrentLine();

public:
    class PythonHighlighter : public QSyntaxHighlighter {
    public:
        PythonHighlighter(QTextDocument* parent)
            : QSyntaxHighlighter(parent)
        {
            QTextCharFormat keyword;
            keyword.setForeground(QColor(86, 156, 214));

            const QStringList keywords = { "def",    "class",  "if",    "else", "elif",   "return",
                                           "import", "from",   "as",    "pass", "break",  "continue",
                                           "for",    "while",  "in",    "try",  "except", "finally",
                                           "with",   "lambda", "yield", "None", "True",   "False" };

            for (const auto& kw : keywords)
                rules.append({ QRegularExpression("\\b" + kw + "\\b"), keyword });

            QTextCharFormat stringFmt;
            stringFmt.setForeground(QColor(206, 145, 120));
            rules.append({ QRegularExpression("\".*\""), stringFmt });
            rules.append({ QRegularExpression("\'.*\'"), stringFmt });

            QTextCharFormat commentFmt;
            commentFmt.setForeground(QColor(106, 153, 85));
            rules.append({ QRegularExpression("#[^\n]*"), commentFmt });
        }

    protected:
        void highlightBlock(const QString& text) override
        {
            for (const auto& rule : rules) {
                auto it = rule.pattern.globalMatch(text);
                while (it.hasNext()) {
                    auto m = it.next();
                    setFormat(static_cast<int>(m.capturedStart()), static_cast<int>(m.capturedLength()), rule.format);
                }
            }
        }

    private:
        struct Rule {
            QRegularExpression pattern;
            QTextCharFormat format;
        };
        QVector<Rule> rules;
    };

    class PythonLineNumberArea : public QWidget {
    public:
        PythonLineNumberArea(PythonEditor* editor)
            : QWidget(editor)
            , m_editor(editor)
        {}

        QSize sizeHint() const override { return QSize(m_editor->lineNumberAreaWidth(), 0); }

    protected:
        void paintEvent(QPaintEvent* event) override { m_editor->lineNumberAreaPaintEvent(event); }

    private:
        PythonEditor* m_editor;
    };

    struct Data {
        PythonEditor* widget = nullptr;
        QWidget* lineNumberArea = nullptr;
        PythonHighlighter* highlighter = nullptr;
    } d;
};

void
PythonEditorPrivate::init()
{
    d.lineNumberArea = new PythonLineNumberArea(d.widget);
    d.highlighter = new PythonHighlighter(d.widget->document());

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();

    QObject::connect(d.widget, &QPlainTextEdit::blockCountChanged, d.widget,
                     [this](int n) { updateLineNumberAreaWidth(n); });

    QObject::connect(d.widget, &QPlainTextEdit::updateRequest, d.widget,
                     [this](const QRect& r, int dy) { updateLineNumberArea(r, dy); });

    QObject::connect(d.widget, &QPlainTextEdit::cursorPositionChanged, d.widget, [this]() { highlightCurrentLine(); });
}

int
PythonEditorPrivate::lineNumberAreaWidth() const
{
    int digits = 1;
    int max = qMax(1, d.widget->blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }

    const QFontMetrics fm(d.widget->font());
    return 10 + fm.horizontalAdvance(QLatin1Char('9')) * digits;
}

void
PythonEditorPrivate::updateLineNumberAreaWidth(int)
{
    d.widget->setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void
PythonEditorPrivate::updateLineNumberArea(const QRect& rect, int dy)
{
    if (dy)
        d.lineNumberArea->scroll(0, dy);
    else
        d.lineNumberArea->update(0, rect.y(), d.lineNumberArea->width(), rect.height());

    if (rect.contains(d.widget->viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void
PythonEditorPrivate::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> extra;

    QTextEdit::ExtraSelection sel;
    sel.format.setBackground(QColor(40, 40, 40));
    sel.format.setProperty(QTextFormat::FullWidthSelection, true);
    sel.cursor = d.widget->textCursor();
    sel.cursor.clearSelection();

    extra.append(sel);
    d.widget->setExtraSelections(extra);
}

PythonEditor::PythonEditor(QWidget* parent)
    : QPlainTextEdit(parent)
    , p(new PythonEditorPrivate())
{
    p->d.widget = this;
    p->init();
}

PythonEditor::~PythonEditor() = default;

int
PythonEditor::lineNumberAreaWidth() const
{
    return p->lineNumberAreaWidth();
}

void
PythonEditor::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect rect = contentsRect();
    p->d.lineNumberArea->setGeometry(QRect(rect.left(), rect.top(), p->lineNumberAreaWidth(), rect.height()));
}

void
PythonEditor::lineNumberAreaPaintEvent(QPaintEvent* event)
{
    QPainter painter(p->d.lineNumberArea);
    painter.fillRect(event->rect(), QColor(30, 30, 30));

    painter.setFont(font());
    const QFontMetrics fm(font());

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const QString number = QString::number(blockNumber + 1);
            painter.setPen(QColor(150, 150, 150));
            painter.drawText(0, top, p->d.lineNumberArea->width() - 4, fm.height(), Qt::AlignRight, number);
        }

        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

}  // namespace stageviz
