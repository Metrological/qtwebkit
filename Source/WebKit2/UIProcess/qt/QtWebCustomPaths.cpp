#include "QtWebCustomPaths.h"

QtWebCustomPaths::QtWebCustomPaths(){};
QtWebCustomPaths::~QtWebCustomPaths(){};

/*static*/ QtWebCustomPaths& QtWebCustomPaths::instance(void)
{
    static QtWebCustomPaths webcustompaths;

    return webcustompaths;
}
     
void QtWebCustomPaths::setPath(const QtWebPathType& type, const QString& path)
{
    Q_ASSERT(true != path.isEmpty());
    Q_ASSERT(true == paths[type].isEmpty());
    Q_ASSERT(MaxPath > type);

    paths[type] = path;
}

const QString& QtWebCustomPaths::getPath(const QtWebPathType& type) const
{
    Q_ASSERT(MaxPath > type);

    return paths[type];
}
