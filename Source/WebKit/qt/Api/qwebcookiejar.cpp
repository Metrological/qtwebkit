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

#include "config.h"
#include "qwebcookiejar.h"
#include "CookieJarQt.h"

static WebCore::SharedCookieJarQt* actualImplementation = NULL;
static SharedCookieJar*            facadeImplementation = NULL;

/* static */ SharedCookieJar* SharedCookieJar::create(const QString& storageLocation)
{
  if (facadeImplementation == NULL)
  {
    facadeImplementation = new SharedCookieJar (storageLocation);
  }

  return (facadeImplementation);
}

SharedCookieJar::SharedCookieJar (const QString& location)
{
  actualImplementation = WebCore::SharedCookieJarQt::create(location);
}

SharedCookieJar::~SharedCookieJar()
{
}

SharedCookieJar::operator QNetworkCookieJar* ()
{
  return (actualImplementation);
}

void SharedCookieJar::destroy()
{
  actualImplementation->destroy();
}

// void SharedCookieJar::getHostnamesWithCookies(HashSet<String>&);

void SharedCookieJar::deleteCookiesForHostname(const QString& name)
{
  actualImplementation->deleteCookiesForHostname(name);
}

void SharedCookieJar::deleteAllCookies()
{
  actualImplementation->deleteAllCookies();
}

bool SharedCookieJar::setCookiesFromUrl(const QList<QNetworkCookie>& cookies, const QUrl& url)
{
  return (actualImplementation->setCookiesFromUrl(cookies, url));
}

void SharedCookieJar::loadCookies()
{
  actualImplementation->loadCookies();
}
