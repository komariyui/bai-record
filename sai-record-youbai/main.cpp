#include "sai_record_widget.h"

#include <QApplication>
#include <QLoggingCategory>

int main(int argc, char *argv[])
{
    qputenv("QT_LOGGING_RULES", "qt.text.font.db=false");
    QLoggingCategory::setFilterRules(QStringLiteral("qt.text.font.db=false"));

    QApplication a(argc, argv);

    // 应用元信息
    a.setApplicationName("SAI Record Youbai");
    a.setApplicationVersion("1.0.0");
    a.setOrganizationName("Youbai");

    SaiRecordWidget w;
    w.show();

    return QApplication::exec();
}
