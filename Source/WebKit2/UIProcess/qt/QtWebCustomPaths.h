#ifndef _QTWEBCUSTOMPATHS_H_
#define _QTWEBCUSTOMPATHS_H_

#include <QString>

class __attribute__((visibility("default"))) QtWebCustomPaths
{
    public:

    enum QtWebPathType {CookieStorage = 0, DatabaseStorage = 1, DiskCacheStorage = 2, IconDatabaseStorage = 3, LocalStorage = 4, MaxPath = 5 /* should be last */};

    ~QtWebCustomPaths();

    /* only a single setter should exist */
    static QtWebCustomPaths& instance(void);
        
    void setPath(const QtWebPathType& type, const QString& path);
    const QString& getPath(const QtWebPathType& type) const;

    private:

    QtWebCustomPaths();

    QtWebCustomPaths(const QtWebCustomPaths&);
    QtWebCustomPaths& operator=(const QtWebCustomPaths&);

    QString paths[QtWebCustomPaths::MaxPath]; // QString() creates NULL, empty, strings
};

#endif // _QTWEBCUSTOMPATHS_H_
