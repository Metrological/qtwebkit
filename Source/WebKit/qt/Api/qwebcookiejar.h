/*
    Copyright (C) 2014 Metrological

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#ifndef cookiejarqt_h
#define cookiejarqt_h

#include <QtCore/QString>
#include <QtNetwork/QNetworkCookieJar>


class __attribute__((visibility("default"))) SharedCookieJar
{
public:
    static SharedCookieJar* create(const QString&);

    operator QNetworkCookieJar* ();

    // void getHostnamesWithCookies(HashSet<String>&);
    void deleteCookiesForHostname(const QString&);
    void deleteAllCookies();
    bool setCookiesFromUrl(const QList<QNetworkCookie>&, const QUrl&);
    void loadCookies();
    void destroy();

private:
    SharedCookieJar(const QString&);
    ~SharedCookieJar();
};

#endif // cookiejarqt_h
